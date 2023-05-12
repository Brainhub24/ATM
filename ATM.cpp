#include <iostream>
#include <cstdlib>

using namespace std;

int money;

// function`s

int Deposit()
{
    int deposit;

    cout << "*****How much do you want to Deposit : *****" << endl;
    cin >> deposit;

    money = money + deposit;

    cout << "_____Now Balance is_____= " << money << endl;

    return 0;
}

int Withdraw()
{
    int withdraw;

    cout << "*****How much do you want to Withdraw*****:" << endl;
    cin >> withdraw;

    money = money - withdraw;

    if (money < 0) {
        cout << " Something went wrong . " << endl;
    }

    cout << "_____Now Balance is_____= " << money << endl;

    return 0;
}

int Balance()
{
    cout << "_____Your Balance is_____= " << money << endl;

    return 0;
}

int Payment()
{
    int payment;
    string bank;
    int num;

    cout << "*****Which Bank ? *****:" << endl;
    cin >> bank;

    cout << "*****Please enter the number of bank account*****:" << endl;
    cin >> num;

    cout << "*****How much do you want do Payment*****:" << endl;
    cin >> payment;

    money = money - payment;

    if (money < 0) {
        cout << " Something went wrong  . " << endl;
    }

    cout << "_____Now Balance is_____= " << money << endl;

    return 0;
}

// function main

int main()
{
    cout << "\033[1;36m"; // set text color to cyan
    cout << "******Welcome to this ATM******" << endl;

    // bank

    cout << "******Enter the amount money : ******" << endl;
    cin >> money;

    int number;

    for (int m = 0; m < 1; m--) {
        cout << "******Deposit (1) ______  Withdraw (2) ______ Balance (3) ______ Payment (4) ______ exit (0) " << " \n ";
        cin >> number;

        switch (number) {
            case 1:
                Deposit();
                break;
            case 2:
                Withdraw();
                break;
            case 3:
                Balance();
                break;
            case 4:
                Payment();
                break;
            case 0:
                exit(0);
                break;
            default:
                break;
        }
    }

    cout << "\033[0m"; // reset text color

    return 0;
}
