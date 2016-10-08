#ifndef SHAREPAGE_H
#define SHAREPAGE_H

#include "sharedatamanager.h"

#include <QDialog>

namespace Ui {
    class SharePage;
}

class ShareTableModel;
class OptionsModel;

QT_BEGIN_NAMESPACE
class QTableView;
class QItemSelection;
class QSortFilterProxyModel;
class QMenu;
class QModelIndex;
QT_END_NAMESPACE

/** Widget that shows a list of sending or receiving addresses.
  */
class SharePage : public QDialog
{
    Q_OBJECT

public:
    enum Tabs {
        SendingTab = 0,
        ReceivingTab = 1
    };

    enum Mode {
        ForSending, /**< Open address book to pick address for sending */
        ForEditing  /**< Open address book for editing */
    };

    explicit SharePage(Mode mode, Tabs tab, QWidget *parent = 0);
    ~SharePage();

    void setModel(ShareTableModel *model);
    void setOptionsModel(OptionsModel *optionsModel);
    const QString &getReturnValue() const { return returnValue; }

public slots:
    void done(int retval);
    void exportClicked();

private:
    Ui::SharePage *ui;
    ShareTableModel *model;
    OptionsModel *optionsModel;
    Mode mode;
    Tabs tab;
    QString returnValue;
    QSortFilterProxyModel *proxyModel;
    QMenu *contextMenu;
    QAction *deleteAction;
    QString newAddressToSelect;
    ShareDataManager ccdb;


private slots:
    void on_giveButton_clicked();
    /** Copy address of currently selected address entry to clipboard */
    void on_copyToClipboard_clicked();
    void on_verifyMessage_clicked();
    void on_refreshButton_clicked();
    void on_visitButton_clicked();


    void selectionChanged();
    void on_showQRCode_clicked();
    /** Spawn contextual menu (right mouse menu) for address book entry */
    void contextualMenu(const QPoint &point);

    /** Copy label of currently selected address entry to clipboard */
    void onCopyLabelAction();

    /** New entry/entries were added to address table */
    void selectNewAddress(const QModelIndex &parent, int begin, int end);

signals:
    void verifyMessage(QString addr);
};

#endif // SHAREPAGE_H
