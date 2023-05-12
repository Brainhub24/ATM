#include <iostream>
#include <string>

using namespace std;

// Global variables
int balance = 0;

// Function prototypes
void showMenu();
void depositMoney();
void withdrawMoney();
void checkBalance();
void makePayment();

int main() {
    // Welcome message
    cout << "Welcome to the ATM" << endl << endl;

    // Display main menu
    showMenu();

    return 0;
}

void showMenu() {
    int option;

    do {
        // Display options
        cout << "Please select an option:" << endl;
        cout << "1. Deposit money" << endl;
        cout << "2. Withdraw money" << endl;
        cout << "3. Check balance" << endl;
        cout << "4. Make a payment" << endl;
        cout << "0. Exit" << endl;

        // Get user input
        cout << "Option: ";
        cin >> option;

        // Handle user input
        switch (option) {
            case 1:
                depositMoney();
                break;
            case 2:
                withdrawMoney();
                break;
            case 3:
                checkBalance();
                break;
            case 4:
                makePayment();
                break;
            case 0:
                cout << "Thank you for using the ATM" << endl;
                break;
            default:
                cout << "Invalid option, please try again" << endl;
        }
    } while (option != 0);
}

void depositMoney() {
    int amount;
    cout << endl << "Enter the amount you want to deposit: ";
    cin >> amount;
    balance += amount;
    cout << "Deposit successful" << endl;
    cout << "Your new balance is: " << balance << endl << endl;
}

void withdrawMoney() {
    int amount;
    cout << endl << "Enter the amount you want to withdraw: ";
    cin >> amount;
    if (amount > balance) {
        cout << "Insufficient funds" << endl;
        cout << "Your current balance is: " << balance << endl << endl;
    } else {
        balance -= amount;
        cout << "Withdrawal successful" << endl;
        cout << "Your new balance is: " << balance << endl << endl;
    }
}

void checkBalance() {
    cout << endl << "Your current balance is: " << balance << endl << endl;
}

void makePayment() {
    string bankName;
    string accountNumber;
    int amount;

    cout << endl << "Please enter the name of the bank: ";
    cin >> bankName;

    cout << "Please enter the account number: ";
    cin >> accountNumber;

    cout << "Please enter the amount to be paid: ";
    cin >> amount;

    if (amount > balance) {
        cout << "Insufficient funds" << endl;
        cout << "Your current balance is: " << balance << endl << endl;
    } else {
        balance -= amount;
        cout << "Payment successful" << endl;
        cout << "Your new balance is: " << balance << endl << endl;
    }
}
