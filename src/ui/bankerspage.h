#pragma once
// OS Management > Deadlock Avoidance: Banker's Algorithm on a SIMULATED
// system (processes P0.. and resource types A, B, C, ...). All calculations are
// done by os::checkSafety / os::requestResources (bankers.h).

#include "bankers.h"
#include "page.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace labdesk::ui {

class MessageBanner;

class BankersPage : public Page {
    Q_OBJECT
public:
    explicit BankersPage(QWidget* parent = nullptr);

private:
    void resizeTables(int processes, int resources);
    void loadState(const os::BankersState& state);
    bool readState(os::BankersState& out);
    bool readVector(const QString& text, os::Vector& out, QString* error) const;
    void updateNeed();
    void onCheckSafety();
    void onRequest(bool release);
    void showSafety(const os::SafetyResult& result, const os::BankersState& state);

    MessageBanner* m_banner = nullptr;
    QSpinBox* m_processCount = nullptr;
    QSpinBox* m_resourceCount = nullptr;
    QTableWidget* m_available = nullptr;
    QTableWidget* m_allocation = nullptr;
    QTableWidget* m_maximum = nullptr;
    QTableWidget* m_need = nullptr;
    QLabel* m_verdict = nullptr;
    QLabel* m_sequence = nullptr;
    QTableWidget* m_steps = nullptr;
    QComboBox* m_requestProcess = nullptr;
    QLineEdit* m_requestVector = nullptr;
    bool m_loading = false;
};

}  // namespace labdesk::ui
