#include <iostream>
#include <cstdlib>

using namespace std;

int money;

// function`s

int Deposit ()
{
    int Deposit;

    cout << "*****How much do you want to Deposit : *****"<< endl;
    cin >> Deposit;

    money = money + Deposit;

    cout << "_____Now Balance is_____= " << money << endl;

}

int Withdraw ()
{
    int Withdraw;

    cout << "*****How much do you want to Withdraw*****:" << endl;
    cin >> Withdraw;

    money = money - Withdraw;

    if ( money < 0){

    cout << " Something went wrong . " << endl;

    }


    cout << "_____Now Balance is_____= " << money << endl;

}

int Balance ()
{
    int Balance;

    cout << "_____Your Balance is_____= " << money << endl;

}

int Payment ()
{
    int Payment;
    string Bank;
    int num;

    cout << "*****Which Bank ? *****:" << endl;
    cin >> Bank;

    cout << "*****Please enter the number of bank account*****:" << endl;
    cin >> num;


    cout << "*****How much do you want do Payment*****:" << endl;
    cin >> Payment;

    money = money - Payment;

    if ( money < 0 ) {

    cout << " Something went wrong  . " << endl;

    }


    cout << "_____Now Balance is_____= " << money << endl;

}
// function main


int main()
{
    system("color 5E");

    cout << "******Welcome to this ATM******" << endl;

    //bank



    cout << "******Enter the amount money : ******" << endl;
    cin >> money;

    int number;


    for (int m = 0; m < 1; m--)
    {


        cout << "******Deposit (1) ______  Withdraw (2) ______ Balance (3) ______ Payment (4) ______ exit (0) "   << " \n ";

        cin >> number;

        switch (number)
        {
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


    return 0;
}

