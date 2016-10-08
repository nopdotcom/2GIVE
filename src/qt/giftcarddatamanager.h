#ifndef GIFTCARDDATAMANAGER_H
#define GIFTCARDDATAMANAGER_H

//#include "walletmodel.h"        // migrateFromBDB4
#include "wallet.h"             // migrateFromBDB4

#include <QSqlDatabase>

struct GiftCardDataEntry
{
    int     id;
    QString pubkey;
    QString privkey;
    QString label;
    QString filename;
    QString generated;
    float   balance;
};

class GiftCardDataManager
{
public:
    explicit GiftCardDataManager();
    explicit GiftCardDataManager(const QString &path, bool &firstRun);
    explicit GiftCardDataManager(QSqlDatabase qdb, bool &firstRun);
    bool addCard(const QString &pubkey, const QString &privkey, const QString &label, const QString &filename = "");
//    bool readCard(const QString &pubkey, QString &privkey, QString &label, QString &filename) const;
    bool readCard(const QString &pubkey, GiftCardDataEntry &card);
    bool readCardAttVal(const QString &pubkey,  const QString &att, QString &val) const;
    bool deleteCard(const QString &pubkey, bool deleteFile = false);
    bool updateCard(const QString &pubkey, const QString &label, const QString &filename = "");
    bool allCards(QList<GiftCardDataEntry> &cards, const QString &sortBy);
    float getBalance(const QString &pubkey);
    bool updateBalances(void);
    bool migrateFromBDB4(CWallet *wallet);
private:
    QString         gdbFilename;
    QSqlDatabase    gdb;

    bool initSchema(void);

};

#endif // GIFTCARDDATAMANAGER_H
