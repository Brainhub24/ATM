#include <iostream>
#include <string>
#include <fstream>
#include <limits>

using namespace std;

// =============================================
// ATM Simulator v2
// Created by Brainhub24/ATM
// =============================================

// - Original base: Simple global balance ATM from Brainhub24/ATM
// - Improvements: PIN auth, file persistence, input validation, error handling
// - Goal: Fully working local ATM simulator
// - All changes are personal creations for educational enhancement

// =============================================
// FUTURE PLANNING PHASES (Next Features Roadmap)
// =============================================

// Phase 1: Implement multi-user support
// - Replace single global balance with user accounts
// - Use struct User { string username; int pin; int balance; string history; }
// - map<string, User> users; or vector<User>
// - Add login/register with username + PIN
// - Load/save users from "users.txt" (simple CSV format)
// - Each user has own balance and transaction log
// - Planning: Start with 2-3 hardcoded demo users, then file-based

// Phase 2: Adding GUI Theme
// - Enhance terminal UI with colors (ANSI escape codes or ncurses)
// - Add "themes": e.g., classic green, modern blue
// - Better formatting: borders, clear screen, progress indicators
// - Future: Full GUI with Qt5 or ImGui for desktop app
// - Planning: Add #ifdef for theme selection at compile

// Phase 3: Add menu loop logic
// - Current do-while is basic; enhance with:
//   - Separate functions: displayMenu(), getValidatedInput()
//   - Sub-menus (e.g., Payment submenu for different types)
//   - Navigation: back to main, confirm exit
//   - History view option in menu
// - Planning: Refactor showMenu() into cleaner state machine

// Phase 4: Footer copyright
// - Add professional footer in output and code
// - Exit message + "© 2026 Brainhub24. All rights reserved."
// - In README or generated reports if expanded
// - Planning: Make copyright string const, print on exit

// =============================================
// END OF PLANNING COMMENTS
// =============================================

const int PIN = 1234;
int balance = 0;

// Function prototypes
void loadBalance();
void saveBalance();
void showMenu();
void depositMoney();
void withdrawMoney();
void checkBalance();
void makePayment();

int main() {
    loadBalance();
    int enteredPin;
    cout << "Welcome to the ATM" << endl;
    cout << "Enter PIN: ";
    cin >> enteredPin;
    if (enteredPin != PIN) {
        cout << "Invalid PIN. Access denied." << endl;
        return 1;
    }
    cout << "Access granted." << endl << endl;
    showMenu();
    saveBalance();
    
    // Footer copyright (Phase 4)
    cout << endl << "© 2026 Brainhub24 ATM Simulator. All rights reserved." << endl;
    return 0;
}

void loadBalance() {
    ifstream file("balance.txt");
    if (file.is_open()) {
        file >> balance;
        file.close();
    }
}

void saveBalance() {
    ofstream file("balance.txt");
    if (file.is_open()) {
        file << balance;
        file.close();
    }
}

void showMenu() {
    int option;
    do {
        cout << "Please select an option:" << endl;
        cout << "1. Deposit money" << endl;
        cout << "2. Withdraw money" << endl;
        cout << "3. Check balance" << endl;
        cout << "4. Make a payment" << endl;
        cout << "0. Exit" << endl;
        cout << "Option: ";
        if (!(cin >> option)) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            option = -1;
        }
        switch (option) {
            case 1: depositMoney(); break;
            case 2: withdrawMoney(); break;
            case 3: checkBalance(); break;
            case 4: makePayment(); break;
            case 0: cout << "Thank you for using the ATM" << endl; break;
            default: cout << "Invalid option, please try again" << endl;
        }
    } while (option != 0);
}

void depositMoney() {
    int amount = 0;
    cout << endl << "Enter the amount to deposit: ";
    if (cin >> amount && amount > 0) {
        balance += amount;
        cout << "Deposit successful. New balance: " << balance << endl << endl;
    } else {
        cout << "Invalid amount." << endl << endl;
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }
}

void withdrawMoney() {
    int amount = 0;
    cout << endl << "Enter the amount to withdraw: ";
    if (cin >> amount && amount > 0) {
        if (amount > balance) {
            cout << "Insufficient funds. Balance: " << balance << endl << endl;
        } else {
            balance -= amount;
            cout << "Withdrawal successful. New balance: " << balance << endl << endl;
        }
    } else {
        cout << "Invalid amount." << endl << endl;
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }
}

void checkBalance() {
    cout << endl << "Current balance: " << balance << endl << endl;
}

void makePayment() {
    string bankName, accountNumber;
    int amount = 0;
    cout << endl << "Bank name: ";
    cin >> bankName;
    cout << "Account number: ";
    cin >> accountNumber;
    cout << "Amount: ";
    if (cin >> amount && amount > 0) {
        if (amount > balance) {
            cout << "Insufficient funds. Balance: " << balance << endl << endl;
        } else {
            balance -= amount;
            cout << "Payment successful. New balance: " << balance << endl << endl;
        }
    } else {
        cout << "Invalid amount." << endl << endl;
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }
}
