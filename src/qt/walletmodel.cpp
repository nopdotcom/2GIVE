#include "walletmodel.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "addresstablemodel.h"
#include "contacttablemodel.h"
#include "sharetablemodel.h"
#include "giftcardtablemodel.h"
#include "transactiontablemodel.h"

#include "ui_interface.h"
#include "wallet.h"
#include "walletdb.h" // for BackupWallet
#include "base58.h"

#include <QSet>
#include <QTimer>
#include <QFile>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QMessageBox>


WalletModel::WalletModel(CWallet *wallet, OptionsModel *optionsModel, QObject *parent) :
    QObject(parent), wallet(wallet), optionsModel(optionsModel), addressTableModel(0), contactTableModel(0), giftCardTableModel(0), shareTableModel(0),
    transactionTableModel(0),
    cachedBalance(0), cachedStake(0), cachedUnconfirmedBalance(0), cachedImmatureBalance(0),
    cachedNumTransactions(0),
    cachedEncryptionStatus(Unencrypted),
    cachedNumBlocks(0)
{
    QMessageBox msgBox;
    msgBox.setWindowTitle("Database Manager");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);

    // Connect to SQLite database
    bool firstRun=false;

    boost::filesystem::path gdbName = GetDataDir() / "wallet.sqlite3";
    QString fqnDatabase = QString::fromStdString(gdbName.string());

    qdb = QSqlDatabase::addDatabase("QSQLITE", "wallet");
    qdb.setDatabaseName(fqnDatabase);

    QFile file(fqnDatabase);
    if (file.exists()) {
        firstRun = false;
    } else {
        firstRun = true;
    }
    if (!qdb.open()) {
        msgBox.setText("Cannot open wallet.sqlite3");
        msgBox.setInformativeText("Notify developers: support@strength-in-numbers.org");
        msgBox.exec();
    }


    gcdb = GiftCardDataManager(qdb, firstRun);
    ccdb = ContactDataManager(qdb, firstRun);


    printf("firstRun = %s\n", firstRun ? "true" : "false");
    if (firstRun) {
        gcdb.migrateFromBDB4(wallet);
        ccdb.migrateFromBDB4(wallet);
    }

    addressTableModel = new AddressTableModel(wallet, this);
    contactTableModel = new ContactTableModel(ccdb, this);

    giftCardTableModel = new GiftCardTableModel(gcdb, this);

// connect to the campaigns database
    firstRun=false;

    boost::filesystem::path cdbName = GetDataDir() / "campaigns.sqlite3";
    QString cfqnDatabase = QString::fromStdString(cdbName.string());

    cdb = QSqlDatabase::addDatabase("QSQLITE", "campaigns");
    cdb.setDatabaseName(cfqnDatabase);

    QFile cfile(cfqnDatabase);
    if (cfile.exists()) {
        firstRun = false;
    } else {
        firstRun = true;
    }
    if (!cdb.open()) {
        msgBox.setText("Cannot open campaigns.sqlite3");
        msgBox.setInformativeText("Refresh Campaigns under Share tab");
        msgBox.exec();
    }

    scdb = ShareDataManager(cdb, firstRun);

    shareTableModel = new ShareTableModel(scdb, this);

    transactionTableModel = new TransactionTableModel(wallet, this);

    // This timer will be fired repeatedly to update the balance
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(pollBalanceChanged()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    // Show progress dialog
//    connect(this, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));

    subscribeToCoreSignals();


}

WalletModel::~WalletModel()
{
    unsubscribeFromCoreSignals();
}

qint64 WalletModel::getBalance() const
{
    return wallet->GetBalance();
}

qint64 WalletModel::getUnconfirmedBalance() const
{
    return wallet->GetUnconfirmedBalance();
}

qint64 WalletModel::getStake() const
{
    return wallet->GetStake();
}

qint64 WalletModel::getImmatureBalance() const
{
    return wallet->GetImmatureBalance();
}

int WalletModel::getNumTransactions() const
{
    int numTransactions = 0;
    {
        LOCK(wallet->cs_wallet);
        numTransactions = wallet->mapWallet.size();
    }
    return numTransactions;
}

void WalletModel::updateStatus()
{
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();

    if(cachedEncryptionStatus != newEncryptionStatus)
        emit encryptionStatusChanged(newEncryptionStatus);
}

void WalletModel::pollBalanceChanged()
{
    if(nBestHeight != cachedNumBlocks)
    {
        // Balance and number of transactions might have changed
        cachedNumBlocks = nBestHeight;
        checkBalanceChanged();
    }
}

void WalletModel::checkBalanceChanged()
{
    qint64 newBalance = getBalance();
    qint64 newStake = getStake();
    qint64 newUnconfirmedBalance = getUnconfirmedBalance();
    qint64 newImmatureBalance = getImmatureBalance();

    if(cachedBalance != newBalance || cachedStake != newStake || cachedUnconfirmedBalance != newUnconfirmedBalance || cachedImmatureBalance != newImmatureBalance)
    {
        cachedBalance = newBalance;
        cachedStake = newStake;
        cachedUnconfirmedBalance = newUnconfirmedBalance;
        cachedImmatureBalance = newImmatureBalance;
        emit balanceChanged(newBalance, newStake, newUnconfirmedBalance, newImmatureBalance);
    }
}

void WalletModel::updateTransaction(const QString &hash, int status)
{
    if(transactionTableModel)
        transactionTableModel->updateTransaction(hash, status);

    // Balance and number of transactions might have changed
    checkBalanceChanged();

    int newNumTransactions = getNumTransactions();
    if(cachedNumTransactions != newNumTransactions)
    {
        cachedNumTransactions = newNumTransactions;
        emit numTransactionsChanged(newNumTransactions);
    }
}

void WalletModel::updateAddressBook(const QString &address, const QString &label, bool isMine, int status)
{
    if ((giftCardTableModel) && (address.contains("Gift")))
        giftCardTableModel->updateEntry(address, label, QString(""), 0.0, status);
    else
        if (addressTableModel)
            addressTableModel->updateEntry(address, label, isMine, status);
}

void WalletModel::updateContact(const QString &address, const QString &label, const QString &email, const QString &url, int status)
{
 if (contactTableModel)
     contactTableModel->updateEntry(address, label, email, url, status);

}

bool WalletModel::validateAddress(const QString &address)
{
    CBitcoinAddress addressParsed(address.toStdString());
    return addressParsed.IsValid();
}

WalletModel::SendCoinsReturn WalletModel::sendCoins(const QList<SendCoinsRecipient> &recipients, const CCoinControl *coinControl)
{
    qint64 total = 0;
    QSet<QString> setAddress;
    QString hex;

    if(recipients.empty())
    {
        return OK;
    }

printf("Pre-check input data for validity\n");

    // Pre-check input data for validity
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        if(!validateAddress(rcp.address))
        {
            return InvalidAddress;
        }
        setAddress.insert(rcp.address);

        if(rcp.amount <= 0)
        {
            return InvalidAmount;
        }
        total += rcp.amount;
    }

    if(recipients.size() > setAddress.size())
    {
        return DuplicateAddress;
    }

    int64 nBalance = 0;
    std::vector<COutput> vCoins;
printf("wallet->AvailableCoins\n");
    wallet->AvailableCoins(vCoins, true, coinControl);

printf("BOOST_FOREACH()\n");
    BOOST_FOREACH(const COutput& out, vCoins)
        nBalance += out.tx->vout[out.i].nValue;

    if (total > nBalance)
    {
        return AmountExceedsBalance;
    }

    if ((total + nTransactionFee + nCharityFee) > nBalance)
    {
        return SendCoinsReturn(AmountWithFeeExceedsBalance, nTransactionFee, nCharityFee);
    }

    {
        LOCK2(cs_main, wallet->cs_wallet);
printf("Sendmany\n");
        // Sendmany
        std::vector<std::pair<CScript, int64> > vecSend;
        foreach(const SendCoinsRecipient &rcp, recipients)
        {
            CScript scriptPubKey;
            scriptPubKey.SetDestination(CBitcoinAddress(rcp.address.toStdString()).Get());
            vecSend.push_back(make_pair(scriptPubKey, rcp.amount));
        }

        CWalletTx wtx;
        CReserveKey keyChange(wallet);
        int64 nFeeRequired = 0;
        int64 nCharityRequired = 0;

printf("wallet->CreateTransaction\n");
        bool fCreated = wallet->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nCharityRequired, coinControl);

        if(!fCreated)
        {
            if((total + nFeeRequired + nCharityRequired) > nBalance) // FIXME: could cause collisions in the future
            {
                return SendCoinsReturn(AmountWithFeeExceedsBalance, nFeeRequired, nCharityRequired);
            }
            return TransactionCreationFailed;
        }
        if(!uiInterface.ThreadSafeAskFee(nFeeRequired, tr("Sending...").toStdString()))
        {
            return Aborted;
        }
        if(!wallet->CommitTransaction(wtx, keyChange))
        {
            return TransactionCommitFailed;
        }
        hex = QString::fromStdString(wtx.GetHash().GetHex());
    }

printf("Add addresses / update labels in address book\n");
    // Add addresses / update labels that we've sent to to the address book
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        std::string strAddress = rcp.address.toStdString();
        CTxDestination dest = CBitcoinAddress(strAddress).Get();
        std::string strLabel = rcp.label.toStdString();
        {
            LOCK(wallet->cs_wallet);

            std::map<CTxDestination, std::string>::iterator mi = wallet->mapAddressBook.find(dest);

            // Check if we have a new address or an updated label
            if (mi == wallet->mapAddressBook.end() || mi->second != strLabel)
            {
                wallet->SetAddressBookName(dest, strLabel);
            }
        }
    }
printf("SendCoinsReturn(OK, 0, 0, hex)\n");
    return SendCoinsReturn(OK, 0, 0, hex);
}

OptionsModel *WalletModel::getOptionsModel()
{
    return optionsModel;
}

AddressTableModel *WalletModel::getAddressTableModel()
{
    return addressTableModel;
}

ContactTableModel *WalletModel::getContactTableModel()
{
    return contactTableModel;
}


GiftCardTableModel *WalletModel::getGiftCardTableModel()
{
    return giftCardTableModel;
}

ShareTableModel *WalletModel::getShareTableModel()
{
    return shareTableModel;
}

TransactionTableModel *WalletModel::getTransactionTableModel()
{
    return transactionTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const
{
    if(!wallet->IsCrypted())
    {
        return Unencrypted;
    }
    else if(wallet->IsLocked())
    {
        return Locked;
    }
    else
    {
        return Unlocked;
    }
}

bool WalletModel::setWalletEncrypted(bool encrypted, const SecureString &passphrase)
{
    if(encrypted)
    {
        // Encrypt
        return wallet->EncryptWallet(passphrase);
    }
    else
    {
        // Decrypt -- TODO; not supported yet
        return false;
    }
}

bool WalletModel::setWalletLocked(bool locked, const SecureString &passPhrase)
{
    if(locked)
    {
        // Lock
        return wallet->Lock();
    }
    else
    {
        // Unlock
        return wallet->Unlock(passPhrase);
    }
}

bool WalletModel::changePassphrase(const SecureString &oldPass, const SecureString &newPass)
{
    bool retval;
    {
        LOCK(wallet->cs_wallet);
        wallet->Lock(); // Make sure wallet is locked before attempting pass change
        retval = wallet->ChangeWalletPassphrase(oldPass, newPass);
    }
    return retval;
}

bool WalletModel::backupWallet(const QString &filename)
{
    return BackupWallet(*wallet, filename.toLocal8Bit().data());
}

// Handlers for core signals
static void NotifyKeyStoreStatusChanged(WalletModel *walletmodel, CCryptoKeyStore *wallet)
{
    OutputDebugStringF("NotifyKeyStoreStatusChanged\n");
    QMetaObject::invokeMethod(walletmodel, "updateStatus", Qt::QueuedConnection);
}

static void NotifyAddressBookChanged(WalletModel *walletmodel, CWallet *wallet, const CTxDestination &address, const std::string &label, bool isMine, ChangeType status)
{
    OutputDebugStringF("NotifyAddressBookChanged %s %s isMine=%i status=%i\n", CBitcoinAddress(address).ToString().c_str(), label.c_str(), isMine, status);
    QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(CBitcoinAddress(address).ToString())),
                              Q_ARG(QString, QString::fromStdString(label)),
                              Q_ARG(bool, isMine),
                              Q_ARG(int, status));
}

static void NotifyContactChanged(WalletModel *walletmodel, QString &address, QString &label, QString &email, QString &url, ChangeType status)
{
//    OutputDebugStringF("NotifyContactChanged %s %s status=%i\n", CBitcoinAddress(address).ToString().c_str(), label.c_str(), status);
    QMetaObject::invokeMethod(walletmodel, "updateContact", Qt::QueuedConnection,
                              Q_ARG(QString, address),
                              Q_ARG(QString, label),
                              Q_ARG(QString, email),
                              Q_ARG(QString, url),
                              Q_ARG(int, status));
}

static void NotifyGiftCardChanged(WalletModel *walletmodel, CWallet *wallet, const CTxDestination &address, const std::string &label, bool isMine, ChangeType status)
{
    OutputDebugStringF("NotifyGiftCardChanged %s %s isMine=%i status=%i\n", CBitcoinAddress(address).ToString().c_str(), label.c_str(), isMine, status);
    QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(CBitcoinAddress(address).ToString())),
                              Q_ARG(QString, QString::fromStdString(label)),
                              Q_ARG(bool, isMine),
                              Q_ARG(int, status));
}

static void NotifyTransactionChanged(WalletModel *walletmodel, CWallet *wallet, const uint256 &hash, ChangeType status)
{
    OutputDebugStringF("NotifyTransactionChanged %s status=%i\n", hash.GetHex().c_str(), status);
    QMetaObject::invokeMethod(walletmodel, "updateTransaction", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(hash.GetHex())),
                              Q_ARG(int, status));
}


void WalletModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyStatusChanged.connect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.connect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5));
    wallet->NotifyGiftCardChanged.connect(boost::bind(NotifyGiftCardChanged, this, _1, _2, _3, _4, _5));
    wallet->NotifyTransactionChanged.connect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
}

void WalletModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyStatusChanged.disconnect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.disconnect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5));
    wallet->NotifyContactChanged.disconnect(boost::bind(NotifyContactChanged, this, _1, _2, _3, _4, _5));
    wallet->NotifyGiftCardChanged.disconnect(boost::bind(NotifyGiftCardChanged, this, _1, _2, _3, _4, _5));
    wallet->NotifyTransactionChanged.disconnect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
}

// WalletModel::UnlockContext implementation
WalletModel::UnlockContext WalletModel::requestUnlock()
{
    bool was_locked = getEncryptionStatus() == Locked;
    if(was_locked)
    {
        // Request UI to unlock wallet
        emit requireUnlock();
    }
    // If wallet is still locked, unlock was failed or cancelled, mark context as invalid
    bool valid = getEncryptionStatus() != Locked;

    return UnlockContext(this, valid, was_locked);
}

WalletModel::UnlockContext::UnlockContext(WalletModel *wallet, bool valid, bool relock):
        wallet(wallet),
        valid(valid),
        relock(relock)
{
}

WalletModel::UnlockContext::~UnlockContext()
{
    if(valid && relock)
    {
        wallet->setWalletLocked(true);
    }
}

void WalletModel::UnlockContext::CopyFrom(const UnlockContext& rhs)
{
    // Transfer context; old object no longer relocks wallet
    *this = rhs;
    rhs.relock = false;
}

 bool WalletModel::getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
 {
     return wallet->GetPubKey(address, vchPubKeyOut);   
 }
 
 // returns a list of COutputs from COutPoints
 void WalletModel::getOutputs(const std::vector<COutPoint>& vOutpoints, std::vector<COutput>& vOutputs)
 {
     BOOST_FOREACH(const COutPoint& outpoint, vOutpoints)
     {
         if (!wallet->mapWallet.count(outpoint.hash)) continue;
         COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, wallet->mapWallet[outpoint.hash].GetDepthInMainChain());
         vOutputs.push_back(out);
     }
 }
 
 // AvailableCoins + LockedCoins grouped by wallet address (put change in one group with wallet address) 
 void WalletModel::listCoins(std::map<QString, std::vector<COutput> >& mapCoins) const
 {
     std::vector<COutput> vCoins;
     wallet->AvailableCoins(vCoins);
     std::vector<COutPoint> vLockedCoins;
 
     // add locked coins
     BOOST_FOREACH(const COutPoint& outpoint, vLockedCoins)
     {
         if (!wallet->mapWallet.count(outpoint.hash)) continue;
         COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, wallet->mapWallet[outpoint.hash].GetDepthInMainChain());
         vCoins.push_back(out);
     }
 
     BOOST_FOREACH(const COutput& out, vCoins)
     {
         COutput cout = out;
 
         while (wallet->IsChange(cout.tx->vout[cout.i]) && cout.tx->vin.size() > 0 && wallet->IsMine(cout.tx->vin[0]))
         {
             if (!wallet->mapWallet.count(cout.tx->vin[0].prevout.hash)) break;
             cout = COutput(&wallet->mapWallet[cout.tx->vin[0].prevout.hash], cout.tx->vin[0].prevout.n, 0);
         }
 
         CTxDestination address;
         if(!ExtractDestination(cout.tx->vout[cout.i].scriptPubKey, address)) continue;
         mapCoins[CBitcoinAddress(address).ToString().c_str()].push_back(out);
     }
 }
 
 bool WalletModel::isLockedCoin(uint256 hash, unsigned int n) const
 {
     return false;
 }
 
 void WalletModel::lockCoin(COutPoint& output)
 {
     return;
 }
 
 void WalletModel::unlockCoin(COutPoint& output)
 {
     return;
 }
 
 void WalletModel::listLockedCoins(std::vector<COutPoint>& vOutpts)
 {
     return;
 }


 GiftCardDataManager WalletModel::giftCardDataBase(void)
 {
     return gcdb;
 }

 ShareDataManager WalletModel::shareDataBase(void)
 {
     return scdb;
 }
