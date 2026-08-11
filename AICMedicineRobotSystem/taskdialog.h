#ifndef TASKDIALOG_H
#define TASKDIALOG_H

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPushButton;

class TaskDialog : public QDialog
{
    Q_OBJECT

public:
    TaskDialog(QWidget *parent = nullptr);
    ~TaskDialog();

    QString fromLocation() const;
    QString toLocation() const;
    QString fromStationId() const;   // 取药点站点ID(S1~S4, 与车端协议一致)
    QString toStationId() const;     // 目的地站点ID(S1~S4)
    QString priority() const;
    QString medicines() const;

private slots:
    void onConfirmClicked();
    void onCancelClicked();

private:
    void setupUI();

    QComboBox *fromCombo;
    QComboBox *toCombo;
    QComboBox *priorityCombo;
    QLineEdit *medicineEdit;
    QPushButton *confirmButton;
    QPushButton *cancelButton;
};

#endif
