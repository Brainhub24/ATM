// ============================================================================
//  Brainhub24 ATM Simulator v3 (CLI edition)
// ============================================================================
//
//  A terminal ATM that behaves like the GUI demo in demo/index.html:
//
//   - Wallet with three demo cards (normal, low balance, expired); the PIN of
//     each card is on its "sticky note" on the start screen.
//   - Card reader, language choice (English / Deutsch), PIN entry with masked
//     keypad input and card capture after 3 wrong PINs (kept across runs).
//   - Fast cash, other amount, balance, cash deposit from your wallet,
//     transfers (own accounts and to another card holder), mini statement,
//     PIN change.
//   - Real-machine behaviour: card is returned before cash, cash is retracted
//     after 30 s, card is captured if not taken, "Do you need more time?"
//     after 30 s of inactivity, note mix limited by cassette stock, daily
//     withdrawal limit, multiples of EUR 10.
//   - Receipts with ATM ID, transaction number and auth code, an electronic
//     journal and cassette levels (operator view).
//   - Colour themes, autoplay tour, scripted (piped) input for testing.
//
//  Build:  g++ -std=c++17 -O2 -Wall -Wextra ATM.cpp -o atm
//  Run:    ./atm            (./atm --help for all options)
//
//  All names, card numbers and the bank are fictional. Money is handled as
//  64-bit integer cents. State is saved outside the repository (see --help).
//
//  Created by Brainhub24/ATM. © 2026 Brainhub24. All rights reserved.
// ============================================================================

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

using Cents = long long;

static const char* const VERSION = "3.0.0";
static const char* const COPYRIGHT = "© 2026 Brainhub24. All rights reserved.";
static const char* const ATM_ID = "BH24-0042";
static const int DENOMS[] = {50, 20, 10};
static const int MAX_NOTES = 40;

// ============================================================================
//  Options and globals
// ============================================================================

struct Options {
    std::string theme = "brand";
    std::string statePath;
    bool color = true;
    bool fast = false;
    bool sound = false;
    bool autoplay = false;
    bool reset = false;
    bool plain = false;  // type a line + Enter per screen, no full-screen redraw
    int timeout = 30;  // inactivity timeout in seconds, 0 = off
};

static Options opt;
static bool g_tty = false;      // interactive terminal: raw keys + redraw in place
static bool g_color = false;
static bool g_eof = false;      // input ended: unwind and quit
static bool g_persist = true;   // false in autoplay (tour runs on a fresh copy)
static bool g_saveFailed = false;
static bool g_de = false;       // session language
static int g_spin = 0;
static std::string g_notice;    // one-shot message under the machine

static void pause(int ms) {
    if (opt.fast || ms <= 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static std::tm localTm(long long ms) {
    std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

static std::string strftimeMs(long long ms, const char* fmt) {
    std::tm tm = localTm(ms);
    char buf[64];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}

static std::string today() { return strftimeMs(nowMs(), "%Y-%m-%d"); }

static void bell() {
    if (opt.sound) {
        std::fputs("\a", stdout);
        std::fflush(stdout);
    }
}

// ============================================================================
//  Platform: terminal and input
// ============================================================================

namespace plat {
#ifdef _WIN32
// Windows console: keys come from _getch() (no echo, no Enter needed), colours
// and cursor control use the console's VT mode (Windows 10 and later).
static bool keys = false;   // stdin is a console: read single keys
static bool vtOk = false;   // console accepted VT escape sequences
static DWORD origOutMode = 0;
static bool outModeSaved = false;
bool stdinTty() { return _isatty(_fileno(stdin)); }
bool stdoutTty() { return _isatty(_fileno(stdout)) && vtOk; }
void rawOn() { keys = stdinTty(); }
void rawOff() {
    keys = false;
    if (outModeSaved) SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), origOutMode);
}
int waitReadable(int ms) {
    if (!keys) return 1;  // line input: fgets blocks, no timeouts
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
        if (_kbhit()) return 1;
        if (ms >= 0 && std::chrono::steady_clock::now() >= end) return 0;
        Sleep(15);
    }
}
long readSome(char* b, size_t n) {
    if (keys) {
        int c = _getch();
        if (c == 0 || c == 0xE0) {  // arrow / function key: swallow the second byte
            _getch();
            return readSome(b, n);
        }
        if (c == 3) std::raise(SIGINT);  // Ctrl+C
        b[0] = static_cast<char>(c);
        return 1;
    }
    if (!std::fgets(b, static_cast<int>(n), stdin)) return 0;
    return static_cast<long>(std::strlen(b));
}
void flushInput() {
    if (keys)
        while (_kbhit()) _getch();
}
void setupConsole() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        origOutMode = mode;
        outModeSaved = true;
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
        vtOk = SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT) != 0;
    }
    static char outBuf[1 << 16];  // whole frames per write, so UTF-8 is never split
    std::setvbuf(stdout, outBuf, _IOFBF, sizeof outBuf);
}
#else
static termios orig;
static bool raw = false;
bool stdinTty() { return isatty(0); }
bool stdoutTty() { return isatty(1); }
void rawOn() {
    if (tcgetattr(0, &orig) != 0) return;
    termios t = orig;
    t.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &t) == 0) raw = true;
}
void rawOff() {
    if (raw) tcsetattr(0, TCSAFLUSH, &orig);
    raw = false;
}
int waitReadable(int ms) {
    pollfd p{0, POLLIN, 0};
    int r = poll(&p, 1, ms);
    if (r < 0) return errno == EINTR ? 0 : -1;
    return r == 0 ? 0 : 1;
}
long readSome(char* b, size_t n) { return static_cast<long>(read(0, b, n)); }
void flushInput() {
    if (raw) tcflush(0, TCIFLUSH);
}
void setupConsole() {}
#endif
}  // namespace plat

static void termEnter() {
    if (!g_tty) return;
    plat::rawOn();
    std::fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout);
    std::fflush(stdout);
}

static void termLeave() {
    if (!g_tty) return;
    std::fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
    std::fflush(stdout);
    plat::rawOff();
}

static void onSignal(int) {
    static const char seq[] = "\x1b[0m\x1b[?25h\x1b[?1049l\nATM session aborted.\n";
    if (g_tty) {
#ifdef _WIN32
        std::fwrite(seq, 1, sizeof seq - 1, stdout);
        std::fflush(stdout);
#else
        ssize_t r = write(1, seq, sizeof seq - 1);
        (void)r;
#endif
        plat::rawOff();
    }
    std::_Exit(130);
}

enum class K { Char, Enter, Back, Esc, Timeout, Eof, None };
struct Key {
    K k;
    char c = 0;
};

// All input goes through here: raw keys on a terminal, whole lines from a pipe,
// or a scripted tour in autoplay mode.
struct Input {
    std::string buf;
    bool eof = false;
    std::deque<std::pair<int, char>> script;  // (delay ms, key); '\n' = Enter

    bool keyMode() const { return opt.autoplay || g_tty; }

    int fill(int timeoutMs) {
        if (eof) return -1;
        int w = plat::waitReadable(timeoutMs);
        if (w == 0) return 0;
        if (w < 0) {
            eof = true;
            return -1;
        }
        char tmp[512];
        long n = plat::readSome(tmp, sizeof tmp);
        if (n < 0 && errno == EINTR) return 0;
        if (n <= 0) {
            eof = true;
            return -1;
        }
        buf.append(tmp, static_cast<size_t>(n));
        return 1;
    }

    static int remaining(std::chrono::steady_clock::time_point deadline, int timeoutMs) {
        if (timeoutMs < 0) return -1;
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        return left > 0 ? static_cast<int>(left) : 0;
    }

    Key scripted() {
        if (script.empty()) {
            g_eof = true;
            return {K::Eof};
        }
        auto [delay, ch] = script.front();
        script.pop_front();
        if (!opt.fast) {
            for (int waited = 0; waited < delay; waited += 50) {
                if (g_tty && plat::waitReadable(0) > 0) {  // any key stops the tour
                    char tmp[64];
                    plat::readSome(tmp, sizeof tmp);
                    script.clear();
                    g_eof = true;
                    return {K::Eof};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        if (ch == '\n') return {K::Enter};
        if (ch == 27) return {K::Esc};
        return {K::Char, ch};
    }

    Key next(int timeoutMs) {
        if (g_eof) return {K::Eof};
        if (opt.autoplay) return scripted();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
        for (;;) {
            if (buf.empty()) {
                int r = fill(remaining(deadline, timeoutMs));
                if (r == 0) return {K::Timeout};
                if (r < 0) {
                    g_eof = true;
                    return {K::Eof};
                }
            }
            unsigned char c = static_cast<unsigned char>(buf[0]);
            buf.erase(0, 1);
            if (c == '\r' || c == '\n') return {K::Enter};
            if (c == 127 || c == 8) return {K::Back};
            if (c == 27) {
                if (buf.empty()) fill(30);
                if (!buf.empty() && (buf[0] == '[' || buf[0] == 'O')) {  // arrow keys etc.: ignore
                    buf.erase(0, 1);
                    while (!buf.empty()) {
                        unsigned char e = static_cast<unsigned char>(buf[0]);
                        buf.erase(0, 1);
                        if (e >= 0x40 && e <= 0x7e) break;
                    }
                    continue;
                }
                return {K::Esc};
            }
            if (c == 'x' || c == 'X') return {K::Esc};
            if (c >= 32 && c < 127) return {K::Char, static_cast<char>(c)};
        }
    }

    // 1 = line read, 0 = timeout, -1 = end of input
    int nextLine(int timeoutMs, std::string& out) {
        if (g_eof) return -1;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
        for (;;) {
            size_t nl = buf.find('\n');
            if (nl != std::string::npos) {
                out = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                if (!out.empty() && out.back() == '\r') out.pop_back();
                return 1;
            }
            int r = fill(remaining(deadline, timeoutMs));
            if (r == 0) return 0;
            if (r < 0) {
                if (!buf.empty()) {
                    out = buf;
                    buf.clear();
                    return 1;
                }
                g_eof = true;
                return -1;
            }
        }
    }
};

static Input in;

// ============================================================================
//  Text helpers
// ============================================================================

// Visible width: UTF-8 code points, ANSI escape sequences excluded.
static int vis(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0x1b && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !(s[i] >= 0x40 && s[i] <= 0x7e)) ++i;
            continue;
        }
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

// Cut plain (unstyled) text to w code points.
static std::string cut(const std::string& s, int w) {
    int n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (n == w) return s.substr(0, i);
            ++n;
        }
    }
    return s;
}

static std::string rep(const std::string& s, int n) {
    std::string r;
    for (int i = 0; i < n; ++i) r += s;
    return r;
}

static std::string padR(const std::string& s, int w) { return s + std::string(std::max(0, w - vis(s)), ' '); }
static std::string padL(const std::string& s, int w) { return std::string(std::max(0, w - vis(s)), ' ') + s; }
static std::string centre(const std::string& s, int w) {
    int p = std::max(0, (w - vis(s)) / 2);
    return std::string(p, ' ') + s + std::string(std::max(0, w - p - vis(s)), ' ');
}

static std::vector<std::string> wrap(const std::string& text, int w) {
    std::vector<std::string> out;
    std::istringstream ss(text);
    std::string word, line;
    while (ss >> word) {
        if (!line.empty() && vis(line) + 1 + vis(word) > w) {
            out.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

static std::string upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// ============================================================================
//  Translations (same texts as the GUI demo)
// ============================================================================

struct Str {
    const char* key;
    const char* en;
    const char* de;
};

static const Str STRINGS[] = {
    {"welcome", "Welcome", "Willkommen"},
    {"insert", "Please insert your card", "Bitte Karte einführen"},
    {"insertSub", "Chip side up, chip first.", "Chip nach oben, Chip zuerst."},
    {"noCash", "Withdrawals unavailable · deposits only", "Auszahlung nicht möglich · nur Einzahlungen"},
    {"reading", "Reading your card", "Ihre Karte wird gelesen"},
    {"dontRemove", "Please do not remove your card.", "Bitte entnehmen Sie Ihre Karte nicht."},
    {"expired", "This card has expired", "Diese Karte ist abgelaufen"},
    {"expiredSub", "Please contact your bank for a replacement card.", "Bitte wenden Sie sich für eine Ersatzkarte an Ihre Bank."},
    {"pinTitle", "Please enter your PIN", "Bitte geben Sie Ihre PIN ein"},
    {"pinSub", "Cover the keypad with your hand. Press ENTER to confirm.", "Verdecken Sie die Tastatur mit der Hand. Mit ENTER bestätigen."},
    {"pinShort", "Your PIN has at least 4 digits.", "Ihre PIN hat mindestens 4 Ziffern."},
    {"wrongPin1", "Incorrect PIN. 1 attempt remaining.", "Falsche PIN. Noch 1 Versuch."},
    {"wrongPinN", "Incorrect PIN. {} attempts remaining.", "Falsche PIN. Noch {} Versuche."},
    {"checkingPin", "Checking your PIN", "Ihre PIN wird geprüft"},
    {"wait", "Please wait", "Bitte warten"},
    {"retained", "Your card has been retained", "Ihre Karte wurde einbehalten"},
    {"retainedSub", "The PIN was entered incorrectly 3 times. For your security this ATM has kept the card. Please contact your bank.",
     "Die PIN wurde 3-mal falsch eingegeben. Zu Ihrer Sicherheit wurde die Karte einbehalten. Bitte wenden Sie sich an Ihre Bank."},
    {"notTaken", "Card retained", "Karte einbehalten"},
    {"notTakenSub", "The card was not taken in time and has been kept by the ATM for your security.",
     "Die Karte wurde nicht rechtzeitig entnommen und zu Ihrer Sicherheit einbehalten."},
    {"menu", "Select a service", "Bitte wählen Sie"},
    {"other", "Other amount", "Anderer Betrag"},
    {"balance", "Balance", "Kontostand"},
    {"deposit", "Deposit cash", "Einzahlung"},
    {"transfer", "Transfer", "Überweisung"},
    {"more", "More services", "Weitere Services"},
    {"statement", "Mini statement", "Umsätze"},
    {"changePin", "Change PIN", "PIN ändern"},
    {"returnCard", "Return card", "Karte zurück"},
    {"back", "Back", "Zurück"},
    {"mainMenu", "Main menu", "Hauptmenü"},
    {"amountTitle", "Enter the amount", "Betrag eingeben"},
    {"amountSub", "Multiples of €10. Notes available:", "Vielfache von 10 €. Verfügbare Scheine:"},
    {"confirm", "Confirm", "Bestätigen"},
    {"cancel", "Cancel", "Abbrechen"},
    {"yes", "Yes", "Ja"},
    {"no", "No", "Nein"},
    {"notMultiple", "Please enter an amount in multiples of €10.", "Bitte einen Betrag in Vielfachen von 10 € eingeben."},
    {"zero", "Please enter an amount.", "Bitte einen Betrag eingeben."},
    {"overLimit", "This exceeds your daily withdrawal limit. You can still withdraw {} today.",
     "Der Betrag übersteigt Ihr Tageslimit. Heute noch verfügbar: {}."},
    {"insufficient", "Insufficient funds. Available balance: {}.", "Nicht genügend Guthaben. Verfügbar: {}."},
    {"noNotes", "This amount cannot be paid with the notes currently in the ATM. Please choose a different amount.",
     "Dieser Betrag kann mit den vorhandenen Scheinen nicht ausgezahlt werden. Bitte wählen Sie einen anderen Betrag."},
    {"tryAgain", "Try another amount", "Anderer Betrag"},
    {"receiptQ", "Would you like a receipt?", "Möchten Sie einen Beleg?"},
    {"processing", "Processing your request", "Ihr Auftrag wird bearbeitet"},
    {"takeCard", "Please take your card", "Bitte entnehmen Sie Ihre Karte"},
    {"takeCardSub", "Your cash will follow.", "Danach erhalten Sie Ihr Geld."},
    {"takeCardEnd", "Thank you. Please remember your card.", "Vielen Dank. Bitte vergessen Sie Ihre Karte nicht."},
    {"counting", "Counting your notes", "Ihre Scheine werden gezählt"},
    {"takeCash", "Please take your cash", "Bitte entnehmen Sie Ihr Geld"},
    {"takeCashSub", "Cash not taken within 30 seconds is retracted.", "Nicht entnommenes Geld wird nach 30 Sekunden eingezogen."},
    {"retracted", "Cash retracted", "Geld eingezogen"},
    {"retractedSub", "The notes were not taken and have been retracted. The amount has been returned to your account.",
     "Die Scheine wurden nicht entnommen und eingezogen. Der Betrag wurde Ihrem Konto gutgeschrieben."},
    {"thanks", "Thank you for banking with Brainhub24", "Vielen Dank für Ihren Besuch"},
    {"takeReceipt", "Please take your receipt.", "Bitte entnehmen Sie Ihren Beleg."},
    {"printing", "Printing your receipt", "Ihr Beleg wird gedruckt"},
    {"balTitle", "Your balances", "Ihre Kontostände"},
    {"checkingAcc", "Current account", "Girokonto"},
    {"savingsAcc", "Savings account", "Sparkonto"},
    {"availToday", "Cash available today", "Heute verfügbar (bar)"},
    {"print", "Print receipt", "Beleg drucken"},
    {"cont", "Continue", "Weiter"},
    {"anotherQ", "Would you like another service?", "Möchten Sie einen weiteren Service nutzen?"},
    {"depTitle", "Insert your notes", "Bitte Scheine einlegen"},
    {"depSub", "Place the notes flat in the open slot. Up to 50 notes, no coins.",
     "Legen Sie die Scheine flach in den offenen Schacht. Bis zu 50 Scheine, keine Münzen."},
    {"depInsert", "Insert wallet notes", "Scheine einlegen"},
    {"depWallet", "In your wallet:", "In Ihrer Geldbörse:"},
    {"depCounted", "Notes counted", "Gezählte Scheine"},
    {"depConfirm", "Deposit", "Einzahlen"},
    {"depReturn", "Return notes", "Scheine zurück"},
    {"depDone", "Deposit complete", "Einzahlung abgeschlossen"},
    {"depReturned", "Please take your notes", "Bitte entnehmen Sie Ihre Scheine"},
    {"newBal", "New balance", "Neuer Kontostand"},
    {"trTitle", "Select a transfer", "Überweisung wählen"},
    {"trToSav", "Current → Savings", "Giro → Sparkonto"},
    {"trFromSav", "Savings → Current", "Sparkonto → Giro"},
    {"trOther", "To another account", "An anderes Konto"},
    {"trAcct", "Enter the account number", "Kontonummer eingeben"},
    {"trAcctSub", "7 digits. Account numbers are on the sticky notes in your wallet.",
     "7 Ziffern. Kontonummern stehen auf den Notizzetteln in der Geldbörse."},
    {"trBadAcct", "No account with this number was found.", "Kein Konto mit dieser Nummer gefunden."},
    {"trOwn", "Use the options for your own accounts instead.", "Bitte nutzen Sie die Optionen für Ihre eigenen Konten."},
    {"trAmt", "Enter the transfer amount", "Überweisungsbetrag eingeben"},
    {"trAmtSub", "Type the amount in cents: 12500 = €125.00", "Betrag in Cent: 12500 = 125,00 €"},
    {"trConfirm", "Please confirm the transfer", "Bitte Überweisung bestätigen"},
    {"trTo", "To", "An"},
    {"trFrom", "From", "Von"},
    {"trAmount", "Amount", "Betrag"},
    {"trDone", "Transfer complete", "Überweisung ausgeführt"},
    {"stmtTitle", "Recent transactions", "Letzte Umsätze"},
    {"noTx", "No transactions yet.", "Noch keine Umsätze."},
    {"newPin", "Enter your new PIN", "Neue PIN eingeben"},
    {"newPinSub", "4 digits. Avoid easy combinations such as 1234.", "4 Ziffern. Vermeiden Sie einfache Folgen wie 1234."},
    {"repeatPin", "Enter your new PIN again", "Neue PIN wiederholen"},
    {"pinMismatch", "The two PINs did not match. Please start again.", "Die PINs stimmen nicht überein. Bitte erneut beginnen."},
    {"pinSame", "The new PIN must be different from the current one.", "Die neue PIN muss sich von der bisherigen unterscheiden."},
    {"pinChanged", "Your PIN has been changed", "Ihre PIN wurde geändert"},
    {"cancelled", "Transaction cancelled", "Vorgang abgebrochen"},
    {"moreTime", "Do you need more time?", "Benötigen Sie mehr Zeit?"},
    {"moreTimeSub", "Your session will end in {} seconds.", "Ihre Sitzung endet in {} Sekunden."},
    {"emptyWallet", "Your wallet has no notes to deposit.", "In Ihrer Geldbörse sind keine Scheine."},
    {"txWithdraw", "ATM withdrawal BH24-0042", "Geldautomat BH24-0042"},
    {"txReversal", "Reversal: cash retracted", "Rückbuchung eingezogenes Geld"},
    {"txDeposit", "Cash deposit BH24-0042", "Bareinzahlung BH24-0042"},
    {"txToSav", "Transfer to savings", "Übertrag auf Sparkonto"},
    {"txFromSav", "Transfer from savings", "Übertrag vom Sparkonto"},
    {"txTo", "Transfer to {}", "Überweisung an {}"},
    {"txFrom", "Transfer from {}", "Überweisung von {}"},
};

static std::string tr(const std::string& key, const std::string& arg = "") {
    for (const Str& s : STRINGS) {
        if (key == s.key) {
            std::string v = g_de ? s.de : s.en;
            size_t p = v.find("{}");
            if (p != std::string::npos) v.replace(p, 2, arg);
            return v;
        }
    }
    return key;
}

// ============================================================================
//  Money formatting
// ============================================================================

static std::string group(unsigned long long v, char sep) {
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), 1, sep);
    return s;
}

// €1,234.50 (English) or 1.234,50 € (German), like the GUI.
static std::string fmt(Cents c) {
    bool neg = c < 0;
    unsigned long long a = neg ? static_cast<unsigned long long>(-(c + 1)) + 1 : static_cast<unsigned long long>(c);
    std::string ct = std::to_string(a % 100);
    if (ct.size() < 2) ct = "0" + ct;
    std::string sign = neg ? "-" : "";
    if (g_de) return sign + group(a / 100, '.') + "," + ct + " €";
    return sign + "€" + group(a / 100, ',') + "." + ct;
}

static std::string fmtWhole(int euros) { return g_de ? std::to_string(euros) + " €" : "€" + std::to_string(euros); }

// 1,234.50 for receipts and the journal.
static std::string fmtPlain(Cents c) {
    bool neg = c < 0;
    unsigned long long a = neg ? static_cast<unsigned long long>(-(c + 1)) + 1 : static_cast<unsigned long long>(c);
    std::string ct = std::to_string(a % 100);
    if (ct.size() < 2) ct = "0" + ct;
    return (neg ? "-" : "") + group(a / 100, ',') + "." + ct;
}

// ============================================================================
//  Bank state
// ============================================================================

struct Tx {
    long long t;
    Cents amt;
    std::string desc;
};

struct Account {
    std::string id, pan, holder, exp, pin, acctNo, day, note;
    int tries = 0;
    bool captured = false;
    Cents checking = 0, savings = 0, limit = 0, withdrawn = 0;
    std::vector<Tx> tx;
};

struct Receipt {
    long long t;
    std::vector<std::string> lines;
};

using Notes = std::map<int, int>;

struct State {
    std::vector<Account> accounts;
    Notes wallet, cassettes, capacity;
    int retractBin = 0;
    long long trn = 3417;
    std::vector<Receipt> receipts;
    std::vector<std::string> journal;
};

static State st;
static int cur = -1;  // index of the card in the reader
static Account& acct() { return st.accounts[static_cast<size_t>(cur)]; }
static std::string last4(const Account& a) { return a.pan.substr(a.pan.size() - 4); }

static std::vector<Tx> seedTx(std::initializer_list<std::tuple<int, const char*, Cents>> entries) {
    std::vector<Tx> v;
    long long now = nowMs();
    for (auto& [days, desc, amt] : entries) v.push_back({now - days * 86400000LL, amt, desc});
    return v;
}

static State defaults() {
    State s;
    Account a;
    a.id = "A"; a.pan = "4921 7300 0042 4821"; a.holder = "ALEX MORGAN"; a.exp = "09/29"; a.pin = "1234";
    a.checking = 245075; a.savings = 1250000; a.acctNo = "1004821"; a.limit = 100000; a.day = today();
    a.tx = seedTx({{12, "Salary · Northwind GmbH", 320000}, {11, "Rent · Hausverwaltung", -98000}, {8, "Card · Grocery store", -6420},
                   {6, "Card · Pharmacy", -1895}, {4, "ATM withdrawal", -10000}, {2, "Transfer from S. Rivera", 2500}, {1, "Card · Bakery", -740}});
    Account b;
    b.id = "B"; b.pan = "5389 1100 7764 7710"; b.holder = "SAM RIVERA"; b.exp = "02/28"; b.pin = "2580";
    b.checking = 8640; b.savings = 0; b.acctNo = "1007710"; b.limit = 50000; b.day = today(); b.note = "low balance";
    b.tx = seedTx({{9, "Student grant", 45000}, {7, "Mobile phone", -2999}, {5, "Card · Café", -460},
                   {2, "Transfer to A. Morgan", -2500}, {1, "Card · Bookshop", -1890}});
    Account c;
    c.id = "C"; c.pan = "4921 7300 0019 3307"; c.holder = "JO PATEL"; c.exp = "03/25"; c.pin = "1111";
    c.checking = 51200; c.savings = 0; c.acctNo = "1003307"; c.limit = 100000; c.day = today(); c.note = "expired card";
    s.accounts = {a, b, c};
    s.wallet = {{10, 0}, {20, 2}, {50, 1}};
    s.cassettes = {{10, 120}, {20, 150}, {50, 90}};
    s.capacity = {{10, 200}, {20, 200}, {50, 200}};
    return s;
}

// ---- persistence: a small tab-separated text file ----

static std::string defaultStatePath() {
    if (const char* p = std::getenv("ATM_STATE_FILE"); p && *p) return p;
    namespace fs = std::filesystem;
    fs::path base;
#ifdef _WIN32
    if (const char* u = std::getenv("APPDATA"); u && *u) base = u;  // %APPDATA%\brainhub24-atm
    else base = ".";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) base = x;
    else if (const char* h = std::getenv("HOME"); h && *h) base = fs::path(h) / ".local" / "share";
    else base = ".";
#endif
    return (base / "brainhub24-atm" / "state.tsv").string();
}

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> v;
    std::string f;
    for (char c : s) {
        if (c == sep) {
            v.push_back(f);
            f.clear();
        } else {
            f += c;
        }
    }
    v.push_back(f);
    return v;
}

static void save() {
    if (!g_persist) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(opt.statePath);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::string tmp = opt.statePath + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) {
            g_saveFailed = true;
            return;
        }
        f << "BH24-ATM-STATE\t1\n";
        f << "trn\t" << st.trn << "\nretract\t" << st.retractBin << "\n";
        for (int d : DENOMS) f << "cassette\t" << d << "\t" << st.cassettes[d] << "\t" << st.capacity[d] << "\n";
        for (int d : DENOMS) f << "wallet\t" << d << "\t" << st.wallet[d] << "\n";
        for (const Account& a : st.accounts) {
            f << "account\t" << a.id << "\t" << a.pan << "\t" << a.holder << "\t" << a.exp << "\t" << a.pin << "\t" << a.tries << "\t"
              << a.captured << "\t" << a.checking << "\t" << a.savings << "\t" << a.acctNo << "\t" << a.limit << "\t" << a.withdrawn
              << "\t" << a.day << "\t" << a.note << "\n";
            for (const Tx& t : a.tx) f << "tx\t" << a.id << "\t" << t.t << "\t" << t.amt << "\t" << t.desc << "\n";
        }
        for (const Receipt& r : st.receipts) {
            f << "receipt\t" << r.t;
            for (const std::string& l : r.lines) f << "\t" << l;
            f << "\n";
        }
        for (const std::string& j : st.journal) f << "journal\t" << j << "\n";
        f.flush();
        if (!f) {
            g_saveFailed = true;
            return;
        }
    }
    fs::rename(tmp, opt.statePath, ec);
    g_saveFailed = static_cast<bool>(ec);
}

static bool load() {
    std::ifstream f(opt.statePath);
    if (!f) return false;
    std::string line;
    if (!std::getline(f, line) || line != "BH24-ATM-STATE\t1") return false;
    State s;
    try {
        while (std::getline(f, line)) {
            auto v = split(line, '\t');
            const std::string& k = v[0];
            if (k == "trn" && v.size() == 2) s.trn = std::stoll(v[1]);
            else if (k == "retract" && v.size() == 2) s.retractBin = std::stoi(v[1]);
            else if (k == "cassette" && v.size() == 4) {
                s.cassettes[std::stoi(v[1])] = std::stoi(v[2]);
                s.capacity[std::stoi(v[1])] = std::stoi(v[3]);
            } else if (k == "wallet" && v.size() == 3) s.wallet[std::stoi(v[1])] = std::stoi(v[2]);
            else if (k == "account" && v.size() == 15) {
                Account a;
                a.id = v[1]; a.pan = v[2]; a.holder = v[3]; a.exp = v[4]; a.pin = v[5]; a.tries = std::stoi(v[6]);
                a.captured = v[7] == "1"; a.checking = std::stoll(v[8]); a.savings = std::stoll(v[9]); a.acctNo = v[10];
                a.limit = std::stoll(v[11]); a.withdrawn = std::stoll(v[12]); a.day = v[13]; a.note = v[14];
                if (a.pan.size() < 4) return false;
                s.accounts.push_back(a);
            } else if (k == "tx" && v.size() == 5) {
                for (Account& a : s.accounts)
                    if (a.id == v[1]) a.tx.push_back({std::stoll(v[2]), std::stoll(v[3]), v[4]});
            } else if (k == "receipt" && v.size() >= 2) {
                s.receipts.push_back({std::stoll(v[1]), std::vector<std::string>(v.begin() + 2, v.end())});
            } else if (k == "journal" && v.size() >= 2) {
                s.journal.push_back(line.substr(8));
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    for (int d : DENOMS)
        if (!s.cassettes.count(d) || !s.capacity.count(d) || s.capacity[d] <= 0) return false;
    if (s.accounts.size() != 3) return false;
    st = s;
    return true;
}

static void rollDay(Account& a) {
    if (a.day != today()) {
        a.day = today();
        a.withdrawn = 0;
    }
}

// Electronic journal, like a real ATM's EJ.
static void jr(const std::string& msg) {
    st.journal.push_back(strftimeMs(nowMs(), "%H:%M:%S") + " " + msg);
    if (st.journal.size() > 300) st.journal.erase(st.journal.begin(), st.journal.end() - 300);
    save();
}

static long long nextTrn() {
    ++st.trn;
    save();
    return st.trn;
}

static std::string authCode() {
    static std::mt19937 rng{std::random_device{}()};
    static const char* abc = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string s;
    for (int i = 0; i < 6; ++i) s += abc[rng() % 36];
    return s;
}

// ---- cash logic ----

static int notesTotal(const Notes& n) {
    int s = 0;
    for (int d : DENOMS) s += (n.count(d) ? n.at(d) : 0) * d;
    return s;
}
static int notesCount(const Notes& n) {
    int s = 0;
    for (int d : DENOMS) s += n.count(d) ? n.at(d) : 0;
    return s;
}
static std::string notesStr(const Notes& n) {
    std::string s;
    for (int d : DENOMS)
        if (n.count(d) && n.at(d)) s += (s.empty() ? "" : " ") + std::to_string(d) + "x" + std::to_string(n.at(d));
    return s;
}
static bool cashAvailable() {
    for (int d : DENOMS)
        if (st.cassettes[d] > 0) return true;
    return false;
}

// Fewest notes (max 40) that the cassettes can actually pay out.
static bool noteMix(int euros, Notes& out) {
    bool found = false;
    int bestCount = 0;
    Notes acc, best;
    std::function<void(size_t, int, int)> rec = [&](size_t i, int rem, int count) {
        if (found && count >= bestCount) return;
        if (rem == 0) {
            found = true;
            bestCount = count;
            best = acc;
            return;
        }
        if (i >= std::size(DENOMS)) return;
        int d = DENOMS[i];
        int max = std::min(st.cassettes[d], rem / d);
        for (int k = max; k >= 0; --k) {
            acc[d] = k;
            rec(i + 1, rem - k * d, count + k);
        }
        acc[d] = 0;
    };
    rec(0, euros, 0);
    if (!found || bestCount > MAX_NOTES) return false;
    out = best;
    return true;
}

// ============================================================================
//  Themes
// ============================================================================

struct Theme {
    std::string base, title, err, ok, dim, key, primary, field, frame, brand, glass;
};
static Theme th;
static const char* RESET = "\x1b[0m";

static bool setTheme(const std::string& name) {
    auto mk = [](int bg, int fg, int title, int err, int ok, int dim, int keyBg, int fieldBg, int fieldFg) {
        auto c = [](const std::string& s) { return "\x1b[" + s + "m"; };
        Theme t;
        t.base = std::string(RESET) + c("48;5;" + std::to_string(bg)) + c("38;5;" + std::to_string(fg));
        t.title = c("1;38;5;" + std::to_string(title));
        t.err = c("1;38;5;" + std::to_string(err));
        t.ok = c("1;38;5;" + std::to_string(ok));
        t.dim = c("38;5;" + std::to_string(dim));
        t.key = c("1;38;5;16;48;5;" + std::to_string(keyBg));
        t.primary = c("1;4;38;5;" + std::to_string(title));
        t.field = c("1;38;5;" + std::to_string(fieldFg) + ";48;5;" + std::to_string(fieldBg));
        t.frame = c("38;5;247");
        t.brand = c("1;38;5;37");
        t.glass = c("38;5;240");
        return t;
    };
    if (name == "brand") th = mk(23, 255, 231, 217, 120, 116, 252, 255, 23);       // teal, like the GUI
    else if (name == "classic") th = mk(16, 40, 46, 203, 46, 28, 34, 46, 16);      // green phosphor
    else if (name == "blue") th = mk(18, 255, 231, 217, 120, 111, 252, 255, 18);   // modern blue
    else if (name == "amber") th = mk(16, 214, 220, 203, 220, 130, 214, 214, 16);  // amber CRT
    else if (name == "mono") {
        th = Theme{RESET, "\x1b[1m", "\x1b[1m", "\x1b[1m", "\x1b[2m", "\x1b[7m", "\x1b[1;4m", "\x1b[7m", "", "\x1b[1m", ""};
    } else {
        return false;
    }
    return true;
}

// Styled segment inside the screen glass (returns to the glass colours afterwards).
static std::string sty(const std::string& style, const std::string& text) {
    if (!g_color || style.empty()) return text;
    return style + text + th.base;
}
// Styled segment outside the glass.
static std::string paint(const std::string& style, const std::string& text) {
    if (!g_color || style.empty()) return text;
    return style + text + RESET;
}

// ============================================================================
//  Screen model and renderer
// ============================================================================

enum Style { ST_NORMAL, ST_TITLE, ST_ERR, ST_BIG, ST_DIM, ST_OK };
enum Field { F_NONE, F_PIN, F_EURO, F_CENTS, F_DIGITS };
enum Led { L_OFF, L_ON, L_BLINK };

struct Line {
    std::string s;
    int style = ST_NORMAL;
};

struct Opt {
    char side;  // 'L' or 'R', like the function keys beside a real ATM screen
    int row;    // 1..4
    std::string label;
    bool primary = false;
    bool disabled = false;
    char key = 0;
};

struct Screen {
    std::string title, text, hint;
    bool err = false, center = true, spinner = false, noTimeout = false, locked = false;
    std::vector<Line> body;
    int field = F_NONE;
    int fmax = 0;
    std::vector<Opt> opts;
};

static const int SW = 62;       // visible width of the screen glass
static const int CW = SW - 2;   // usable content width
static const int SH = 14;       // rows on the screen glass

static struct {
    int card = L_OFF, rcpt = L_OFF, cash = L_OFF;
} led;

static const char* SPIN[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};

static void assignKeys(Screen& s) {
    if (s.field != F_NONE) return;  // field screens use letter keys set by the caller
    int k = 0;
    for (char side : {'L', 'R'})
        for (int row = 1; row <= 4; ++row)
            for (Opt& o : s.opts)
                if (o.side == side && o.row == row) o.key = static_cast<char>('1' + k++);
}

static std::string keyLabel(char k) { return k == '\n' ? "↵" : std::string(1, static_cast<char>(k >= 'a' && k <= 'z' ? k - 32 : k)); }

static std::string fieldStr(const Screen& s, const std::string& buf) {
    std::string v;
    switch (s.field) {
        case F_PIN: {
            int slots = std::max(4, static_cast<int>(buf.size()));
            for (int i = 0; i < slots; ++i) v += (i ? " " : "") + std::string(i < static_cast<int>(buf.size()) ? "●" : "–");
            break;
        }
        case F_EURO: v = fmt((buf.empty() ? 0 : std::stoll(buf)) * 100); break;
        case F_CENTS: v = fmt(buf.empty() ? 0 : std::stoll(buf)); break;
        default: v = buf + std::string(static_cast<size_t>(std::max(0, s.fmax - static_cast<int>(buf.size()))), '_');
    }
    return sty(th.field, "   " + v + "   ");
}

static std::string styled(const Line& l) {
    switch (l.style) {
        case ST_TITLE: return sty(th.title, l.s);
        case ST_ERR: return sty(th.err, l.s);
        case ST_BIG: return sty(th.title, "»  " + l.s + "  «");
        case ST_DIM: return sty(th.dim, l.s);
        case ST_OK: return sty(th.ok, l.s);
        default: return l.s;
    }
}

static std::string ledStr(const char* name, int state) {
    if (state == L_OFF) return paint(th.glass, std::string("○ ") + name);
    if (state == L_ON) return paint("\x1b[1;38;5;46m", std::string("● ") + name);
    return paint("\x1b[5;1;38;5;214m", std::string("◉ ") + name);
}

static void render(const Screen& s, const std::string& buf = "", const std::vector<std::string>& extra = {}) {
    // ---- content lines on the glass ----
    std::vector<std::string> content;
    if (!s.title.empty())
        for (const std::string& w : wrap(s.title, CW - 4)) content.push_back(sty(th.title, w));
    if (!s.text.empty()) {
        content.push_back("");
        for (const std::string& w : wrap(s.text, CW - 6)) content.push_back(sty(s.err ? th.err : "", w));
    }
    if (!s.body.empty()) {
        content.push_back("");
        for (const Line& l : s.body) content.push_back(styled(l));
    }
    if (s.field != F_NONE) {
        content.push_back("");
        content.push_back(fieldStr(s, buf));
    }
    if (s.spinner) {
        content.push_back("");
        content.push_back(sty(th.title, SPIN[g_spin % 10]));
    }

    const int optRows = s.opts.empty() ? 0 : 4;
    const int area = SH - optRows - (optRows ? 1 : 0);
    std::vector<std::string> rows(SH);
    int n = std::min(static_cast<int>(content.size()), area);
    int top = std::max(1, (area - n) / 2);
    if (top + n > area) top = std::max(0, area - n);
    for (int i = 0; i < n; ++i) rows[static_cast<size_t>(top + i)] = centre(content[static_cast<size_t>(i)], CW);

    for (int r = 0; r < optRows; ++r) {
        std::string left, right;
        for (const Opt& o : s.opts) {
            if (o.row != r + 1) continue;
            std::string label = cut(o.label, CW / 2 - 6);
            std::string badge = o.disabled ? sty(th.dim, " · ") : sty(th.key, " " + keyLabel(o.key) + " ");
            std::string text = o.disabled ? sty(th.dim, label) : o.primary ? sty(th.primary, label) : label;
            if (o.side == 'L') left = badge + " " + text;
            else right = text + " " + badge;
        }
        rows[static_cast<size_t>(SH - optRows + r)] = padR(left, CW / 2) + padL(right, CW - CW / 2);
    }

    // ---- the machine ----
    const std::string B = paint(th.frame, "│");
    const int OW = SW + 4;
    std::vector<std::string> out;
    out.push_back("  " + paint(th.frame, "╭" + rep("─", OW) + "╮"));
    std::string brand = paint(th.brand, " ◆ BRAINHUB24 BANK");
    std::string clock = paint(th.frame, std::string("ATM ") + ATM_ID + "  " + strftimeMs(nowMs(), "%d.%m.%Y %H:%M") + " ");
    out.push_back("  " + B + padR(brand, OW - vis(clock)) + clock + B);
    out.push_back("  " + paint(th.frame, "├" + rep("─", OW) + "┤"));
    out.push_back("  " + B + " " + paint(th.glass, "┌" + rep("─", SW) + "┐") + " " + B);
    for (const std::string& r : rows) {
        std::string line = g_color ? th.base + " " + padR(r, CW) + " " + RESET : " " + padR(r, CW) + " ";
        out.push_back("  " + B + " " + paint(th.glass, "│") + line + paint(th.glass, "│") + " " + B);
    }
    out.push_back("  " + B + " " + paint(th.glass, "└" + rep("─", SW) + "┘") + " " + B);
    std::string leds = "  " + ledStr("CARD", led.card) + "    " + ledStr("RECEIPT", led.rcpt) + "    " + ledStr("CASH", led.cash);
    std::string pad = paint(th.glass, "▦ PIN PAD ");
    out.push_back("  " + B + padR(leds, OW - vis(pad)) + pad + B);
    out.push_back("  " + paint(th.frame, "╰" + rep("─", OW) + "╯"));
    for (const std::string& e : extra) out.push_back(e);
    out.push_back("");

    // ---- guidance below the machine ----
    if (!g_notice.empty()) {
        out.push_back("  " + paint("\x1b[1;38;5;203m", "✖ " + g_notice));
        g_notice.clear();
    } else if (!s.hint.empty()) {
        out.push_back("  " + paint("\x1b[38;5;222m", "› " + s.hint));
    } else {
        out.push_back("");
    }
    std::string legend;
    if (s.locked) legend = "Please wait…";
    else if (s.field != F_NONE) {
        legend = "0-9 type · Enter confirm · Backspace correct · Esc/X cancel";
        for (const Opt& o : s.opts)
            if (o.key) legend += " · " + keyLabel(o.key) + " " + o.label;
    } else if (!s.opts.empty()) legend = "Number keys select · Enter = underlined option · Esc/X cancel";
    if (!legend.empty()) out.push_back("  " + paint("\x1b[38;5;245m", legend));
    if (g_saveFailed) out.push_back("  " + paint("\x1b[1;38;5;203m", "⚠ Could not save the ATM state to " + opt.statePath));

    std::string o = g_tty ? "\x1b[H" : "\n";
    for (const std::string& l : out) o += l + (g_tty ? "\x1b[K\n" : "\n");
    if (g_tty) o += "\x1b[J";
    std::fwrite(o.data(), 1, o.size(), stdout);
    std::fflush(stdout);
}

// ============================================================================
//  Hardware: card, cash and receipt art
// ============================================================================

static int cardColour(const Account& a) { return a.id == "A" ? 33 : a.id == "B" ? 170 : 37; }

static std::vector<std::string> cardArt(const Account& a, int indent) {
    std::string ind(static_cast<size_t>(indent), ' ');
    std::string c = g_color ? "\x1b[1;38;5;231;48;5;" + std::to_string(cardColour(a)) + "m" : "";
    auto row = [&](const std::string& s) { return ind + paint(c, " " + padR(s, 28) + " "); };
    return {row("Brainhub24              DEBIT"), row("▣▣"), row(a.pan), row(padR(a.holder, 22) + a.exp)};
}

static std::vector<std::string> cashArt(const Notes& n) {
    std::vector<std::string> v;
    for (int d : DENOMS) {
        int k = n.count(d) ? n.at(d) : 0;
        if (!k) continue;
        int colour = d == 10 ? 174 : d == 20 ? 75 : 215;
        std::string c = g_color ? "\x1b[1;38;5;16;48;5;" + std::to_string(colour) + "m" : "";
        std::string ds = padR(std::to_string(d), 3);
        v.push_back("        " + paint(c, " " + ds + "    DEMO    " + padL(std::to_string(d), 3) + " ") + "  × " + std::to_string(k));
    }
    return v;
}

static std::vector<std::string> paperArt(const std::vector<std::string>& lines, size_t upTo) {
    std::vector<std::string> v;
    std::string c = g_color ? "\x1b[38;5;235;48;5;255m" : "";
    for (size_t i = 0; i < std::min(upTo, lines.size()); ++i) v.push_back("        " + paint(c, "  " + padR(lines[i], 32) + "  "));
    return v;
}

// ============================================================================
//  Interaction primitives
// ============================================================================

enum ResKind { R_OPT, R_ENTER, R_CANCEL, R_EOF };
struct Res {
    int kind;
    int opt = -1;
    std::string val{};
};

static void busy(Screen s, int ms, const std::vector<std::string>& extra = {}) {
    s.locked = true;
    s.opts.clear();
    if (g_tty && !opt.fast && ms > 0) {
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            render(s, "", extra);
            ++g_spin;
            std::this_thread::sleep_for(std::chrono::milliseconds(90));
        }
    } else {
        render(s, "", extra);
        pause(ms);
    }
    plat::flushInput();  // a real ATM ignores keys while it is busy
}

static void info(const std::string& title, const std::string& text, int ms, const std::string& hint = "") {
    Screen s;
    s.title = title;
    s.text = text;
    s.hint = hint;
    busy(s, ms);
}

static Screen spinnerScreen(const std::string& title, const std::string& text = "") {
    Screen s;
    s.title = title;
    s.text = text;
    s.spinner = true;
    return s;
}

static void echoInput(const Screen& s, const std::string& line) {
    if (plat::stdinTty()) return;
    std::string shown = s.field == F_PIN && allDigits(line) ? std::string(line.size(), '*') : line;
    std::printf("  ⌨ %s\n", shown.c_str());
}

// "Do you need more time?" with a 15 second countdown.
static bool moreTime() {
    Screen m;
    m.title = tr("moreTime");
    m.noTimeout = true;
    m.opts = {{'R', 3, tr("yes"), true}, {'R', 4, tr("no")}};
    assignKeys(m);
    bell();
    if (in.keyMode()) {
        for (int n = 15; n > 0;) {
            m.text = tr("moreTimeSub", std::to_string(n));
            render(m);
            Key k = in.next(1000);
            if (k.k == K::Timeout) {
                --n;
                continue;
            }
            if (k.k == K::Eof || k.k == K::Esc) return false;
            if (k.k == K::Enter || (k.k == K::Char && k.c == m.opts[0].key)) return true;
            if (k.k == K::Char && k.c == m.opts[1].key) return false;
        }
        return false;
    }
    m.text = tr("moreTimeSub", "15");
    render(m);
    std::string line;
    if (in.nextLine(15000, line) != 1) return false;
    echoInput(m, line);
    line = trim(line);
    return line.empty() || line == std::string(1, m.opts[0].key);
}

// Show a screen and wait for the customer. Handles the keypad, function keys,
// the inactivity timer and end of input.
static Res run(Screen s) {
    assignKeys(s);
    std::string buf;
    const int timeoutMs = (opt.timeout > 0 && !s.noTimeout) ? opt.timeout * 1000 : -1;
    auto findOpt = [&](char c) {
        for (size_t i = 0; i < s.opts.size(); ++i)
            if (!s.opts[i].disabled && s.opts[i].key && std::tolower(s.opts[i].key) == std::tolower(c)) return static_cast<int>(i);
        return -1;
    };
    auto primary = [&]() {
        for (size_t i = 0; i < s.opts.size(); ++i)
            if (s.opts[i].primary && !s.opts[i].disabled) return static_cast<int>(i);
        return -1;
    };
    for (;;) {
        render(s, buf);
        if (in.keyMode()) {
            Key k = in.next(timeoutMs);
            switch (k.k) {
                case K::Timeout:
                    if (moreTime()) continue;
                    return {g_eof ? R_EOF : R_CANCEL};
                case K::Eof: return {R_EOF};
                case K::Esc: return {R_CANCEL};
                case K::Back:
                    if (s.field != F_NONE && !buf.empty()) buf.pop_back();
                    else bell();
                    continue;
                case K::Enter:
                    if (s.field != F_NONE) return {R_ENTER, -1, buf};
                    if (int p = primary(); p >= 0) return {R_OPT, p};
                    bell();
                    continue;
                case K::Char:
                    if (s.field != F_NONE && k.c >= '0' && k.c <= '9') {
                        if (static_cast<int>(buf.size()) < s.fmax) buf += k.c;
                        else bell();
                        continue;
                    }
                    if (int i = findOpt(k.c); i >= 0) return {R_OPT, i};
                    bell();
                    continue;
                default: continue;
            }
        }
        // Line mode (piped input): one line per screen.
        std::string line;
        int r = in.nextLine(timeoutMs, line);
        if (r == 0) {
            if (moreTime()) continue;
            return {g_eof ? R_EOF : R_CANCEL};
        }
        if (r < 0) return {R_EOF};
        echoInput(s, line);
        line = trim(line);
        if (line == "x" || line == "X" || line == "\x1b") return {R_CANCEL};
        if (line.size() == 1)
            if (int i = findOpt(line[0]); i >= 0 && !(s.field != F_NONE && allDigits(line))) return {R_OPT, i};
        if (s.field != F_NONE) {
            if (line.empty() || (allDigits(line) && static_cast<int>(line.size()) <= s.fmax)) return {R_ENTER, -1, line};
            g_notice = "Invalid input \"" + line + "\". Type up to " + std::to_string(s.fmax) + " digits.";
            continue;
        }
        if (line.empty())
            if (int p = primary(); p >= 0) return {R_OPT, p};
        g_notice = "Invalid choice \"" + line + "\". Type the number shown next to an option.";
        bell();
    }
}

// Wait for the customer to take something (card, cash, receipt) before a deadline.
static bool waitTake(Screen s, int seconds, const std::vector<std::string>& extra, const std::string& what) {
    s.locked = false;
    s.noTimeout = true;
    bell();
    if (in.keyMode()) {
        for (int n = seconds; n > 0;) {
            s.hint = "Press Enter to take " + what + " (" + std::to_string(n) + " s)";
            render(s, "", extra);
            Key k = in.next(1000);
            if (k.k == K::Timeout) {
                --n;
                continue;
            }
            if (k.k == K::Eof || k.k == K::Enter || (k.k == K::Char && k.c == ' ')) return true;
        }
        return false;
    }
    s.hint = "Press Enter to take " + what + " (" + std::to_string(seconds) + " s)";
    render(s, "", extra);
    std::string line;
    int r = in.nextLine(seconds * 1000, line);
    if (r == 1) echoInput(s, line);
    return r != 0;
}

// ============================================================================
//  Hardware actions
// ============================================================================

static void animateCard(const Account& a, bool inward) {
    Screen s;
    s.locked = true;
    s.title = inward ? "" : tr("takeCard");
    const int frames[] = {34, 26, 18, 10, 4};
    if (g_tty && !opt.fast) {
        for (int i = 0; i < 5; ++i) {
            int f = inward ? frames[i] : frames[4 - i];
            render(s, "", cardArt(a, f));
            std::this_thread::sleep_for(std::chrono::milliseconds(140));
        }
    }
    render(s);
}

static bool cardOut(const std::string& title, const std::string& text) {
    Account& a = acct();
    led.card = L_BLINK;
    Screen s;
    s.title = title;
    s.text = text;
    bool taken = waitTake(s, 30, cardArt(a, 4), "your card");
    led.card = L_OFF;
    return taken;
}

static bool presentCash(const Notes& notes, const std::string& title, const std::string& text, Cents amount) {
    led.cash = L_BLINK;
    Screen s;
    s.title = title;
    s.text = text;
    if (amount) s.body = {{fmt(amount), ST_BIG}};
    bool taken = waitTake(s, 30, cashArt(notes), "the notes");
    led.cash = L_OFF;
    return taken;
}

static void collectReceipt(const std::vector<std::string>& lines) {
    st.receipts.push_back({nowMs(), lines});
    if (st.receipts.size() > 12) st.receipts.erase(st.receipts.begin());
    save();
}

static void printReceipt(const std::vector<std::string>& lines) {
    led.rcpt = L_BLINK;
    Screen s = spinnerScreen(tr("printing"), tr("wait"));
    s.locked = true;
    for (size_t i = 1; i <= lines.size(); ++i) {
        if (g_tty && !opt.fast) {
            ++g_spin;
            render(s, "", paperArt(lines, i));
            std::this_thread::sleep_for(std::chrono::milliseconds(70));
        }
    }
    Screen t;
    t.title = tr("takeReceipt");
    waitTake(t, 15, paperArt(lines, lines.size()), "your receipt");
    collectReceipt(lines);  // a receipt left in the printer goes to the wallet too, like the GUI
    led.rcpt = L_OFF;
}

static std::vector<std::string> receipt(const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows, const Account& a) {
    const int W = 32;
    auto line = [&](const std::string& l, const std::string& r) {
        return l + std::string(static_cast<size_t>(std::max(1, W - vis(l) - vis(r))), ' ') + r;
    };
    auto mid = [&](const std::string& s) { return std::string(static_cast<size_t>(std::max(0, (W - vis(s)) / 2)), ' ') + s; };
    std::string rule(W, '-');
    long long now = nowMs();
    std::vector<std::string> v = {mid("BRAINHUB24 BANK"), mid("Demo Branch, Hauptstrasse 1"), mid(std::string("ATM ") + ATM_ID), rule,
                                  line("DATE " + strftimeMs(now, "%d.%m.%Y"), "TIME " + strftimeMs(now, "%H:%M")),
                                  line("CARD", "************" + last4(a)), rule, title};
    for (const auto& [l, r] : rows) v.push_back(line(l, r));
    v.push_back(rule);
    v.push_back(mid("THANK YOU"));
    v.push_back(mid("DEMO RECEIPT"));
    v.push_back(mid("NOT A REAL TRANSACTION"));
    return v;
}

static Line kv(const std::string& label, const std::string& value) {
    const int w = 46;
    std::string l = cut(label, w - vis(value) - 2);
    return {sty(th.dim, l) + std::string(static_cast<size_t>(std::max(2, w - vis(l) - vis(value))), ' ') + sty(th.title, value)};
}

// ============================================================================
//  Session flows (same screens and rules as demo/index.html)
// ============================================================================

enum class Out { Menu, More, Another, End, Cancel, Finished };

static bool returnCard(const std::string& title = "") {
    bool taken = cardOut(title.empty() ? tr("takeCard") : title, tr("takeCardEnd"));
    if (taken) {
        jr("CARD TAKEN");
        return true;
    }
    Account& a = acct();
    a.captured = true;
    save();
    jr("CARD CAPTURED ****" + last4(a) + " - NOT TAKEN");
    info(tr("notTaken"), tr("notTakenSub"), 4000);
    return false;
}

static void finish(bool skipThanks = false) {
    if (!skipThanks) info(tr("thanks"), "", 2200);
    jr("SESSION END");
}

static void endSession(bool byChoice) {
    led.cash = L_OFF;
    if (!byChoice) {
        jr("SESSION CANCELLED");
        info(tr("cancelled"), "", 1100);
    }
    returnCard();
    finish();
}

static Out withdrawError(const std::string& msg);
static Out amountScreen(const std::string& msg = "");

static std::string checkWithdraw(int euros, Notes& notes) {
    Account& a = acct();
    rollDay(a);
    Cents cents = euros * 100LL;
    if (!euros) return tr("zero");
    if (euros % 10) return tr("notMultiple");
    if (cents > a.limit - a.withdrawn) return tr("overLimit", fmt(std::max<Cents>(0, a.limit - a.withdrawn)));
    if (cents > a.checking) return tr("insufficient", fmt(a.checking));
    if (!noteMix(euros, notes)) return tr("noNotes");
    return "";
}

static Out withdraw(int euros, bool wantReceipt) {
    Account& a = acct();
    Cents cents = euros * 100LL;
    Notes notes;
    std::string err = checkWithdraw(euros, notes);
    if (!err.empty()) return withdrawError(err);
    busy(spinnerScreen(tr("processing"), tr("wait")), 1700);
    long long trn = nextTrn();
    std::string auth = authCode();
    a.checking -= cents;
    a.withdrawn += cents;
    a.tx.push_back({nowMs(), -cents, tr("txWithdraw")});
    save();
    jr("TRN " + std::to_string(trn) + " WDL EUR " + fmtPlain(cents) + " AUTH " + auth);

    // Card before cash, so the card cannot be forgotten.
    if (!cardOut(tr("takeCard"), tr("takeCardSub"))) {
        a.checking += cents;
        a.withdrawn -= cents;
        a.tx.pop_back();
        a.captured = true;
        save();
        jr("TRN " + std::to_string(trn) + " REVERSED - CARD NOT TAKEN, CAPTURED");
        info(tr("notTaken"), tr("notTakenSub"), 4500);
        finish(true);
        return Out::Finished;
    }
    jr("CARD TAKEN");
    bell();
    busy(spinnerScreen(tr("counting"), tr("wait")), 900 + notesCount(notes) * 75);
    for (int d : DENOMS) st.cassettes[d] -= notes[d];
    save();
    jr("TRN " + std::to_string(trn) + " NOTES " + notesStr(notes) + " PRESENTED");

    bool cashTaken = presentCash(notes, tr("takeCash"), tr("takeCashSub"), cents);
    if (cashTaken) {
        for (int d : DENOMS) st.wallet[d] += notes[d];
        jr("TRN " + std::to_string(trn) + " CASH TAKEN");
    } else {
        a.checking += cents;
        a.withdrawn -= cents;
        st.retractBin += notesCount(notes);
        a.tx.push_back({nowMs(), cents, tr("txReversal")});
        jr("TRN " + std::to_string(trn) + " CASH RETRACTED " + std::to_string(notesCount(notes)) + " NOTES - REVERSED");
        info(tr("retracted"), tr("retractedSub"), 3500);
    }
    save();
    if (wantReceipt && cashTaken) {
        printReceipt(receipt("CASH WITHDRAWAL",
                             {{"TRN", std::to_string(trn)}, {"AUTH", auth}, {"AMOUNT", "EUR " + fmtPlain(cents)}, {"NOTES", notesStr(notes)},
                              {"BALANCE", "EUR " + fmtPlain(a.checking)}},
                             a));
        jr("TRN " + std::to_string(trn) + " RECEIPT PRINTED");
    }
    finish();
    return Out::Finished;
}

static Out askReceiptThenWithdraw(int euros) {
    Notes notes;
    std::string err = checkWithdraw(euros, notes);
    if (!err.empty()) return withdrawError(err);
    Screen s;
    s.title = tr("receiptQ");
    s.body = {{fmt(euros * 100LL), ST_BIG}};
    s.opts = {{'R', 3, tr("yes")}, {'R', 4, tr("no")}};
    Res r = run(s);
    if (r.kind != R_OPT) return Out::Cancel;
    return withdraw(euros, r.opt == 0);
}

static Out withdrawError(const std::string& msg) {
    bell();
    Screen s;
    s.title = tr("amountTitle");
    s.text = msg;
    s.err = true;
    s.opts = {{'R', 3, tr("tryAgain")}, {'R', 4, tr("mainMenu")}};
    Res r = run(s);
    if (r.kind != R_OPT) return Out::Cancel;
    return r.opt == 0 ? amountScreen() : Out::Menu;
}

static Out amountScreen(const std::string& msg) {
    std::string avail;
    for (int d : {10, 20, 50})
        if (st.cassettes[d] > 0) avail += (avail.empty() ? "" : " · ") + fmtWhole(d);
    Screen s;
    s.title = tr("amountTitle");
    s.text = msg.empty() ? tr("amountSub") + " " + avail : msg;
    s.err = !msg.empty();
    s.field = F_EURO;
    s.fmax = 4;
    s.opts = {{'L', 4, tr("back"), false, false, 'b'}, {'R', 4, tr("confirm"), true, false, '\n'}};
    s.hint = "Type the amount in euros, then press Enter.";
    Res r = run(s);
    if (r.kind == R_OPT) return Out::Menu;
    if (r.kind != R_ENTER) return Out::Cancel;
    return askReceiptThenWithdraw(r.val.empty() ? 0 : std::stoi(r.val));
}

static Out another() {
    Screen s;
    s.title = tr("anotherQ");
    s.opts = {{'R', 3, tr("yes")}, {'R', 4, tr("no")}};
    Res r = run(s);
    if (r.kind != R_OPT) return Out::Cancel;
    return r.opt == 0 ? Out::Menu : Out::End;
}

static Out balanceScreen() {
    Account& a = acct();
    rollDay(a);
    Cents avail = std::max<Cents>(0, std::min(a.checking, a.limit - a.withdrawn));
    jr("BALANCE INQUIRY");
    Screen s;
    s.title = tr("balTitle");
    s.body = {kv(tr("checkingAcc"), fmt(a.checking)), kv(tr("savingsAcc"), fmt(a.savings)), kv(tr("availToday"), fmt(avail))};
    s.opts = {{'R', 3, tr("print")}, {'R', 4, tr("cont"), true}};
    Res r = run(s);
    if (r.kind != R_OPT) return Out::Cancel;
    if (r.opt == 0)
        printReceipt(receipt("BALANCE INQUIRY",
                             {{"CURRENT", "EUR " + fmtPlain(a.checking)}, {"SAVINGS", "EUR " + fmtPlain(a.savings)}, {"CASH TODAY", "EUR " + fmtPlain(avail)}},
                             a));
    return Out::Another;
}

// ---- deposit ----

static Out returnDeposit(const Notes& notes, bool thenEnd) {
    bool taken = presentCash(notes, tr("depReturned"), "", 0);
    for (int d : DENOMS) {
        if (taken) st.wallet[d] += notes.count(d) ? notes.at(d) : 0;
    }
    if (!taken) st.retractBin += notesCount(notes);
    jr(taken ? "DEPOSIT RETURNED TO CUSTOMER" : "DEPOSIT NOTES RETRACTED " + std::to_string(notesCount(notes)));
    save();
    return thenEnd ? Out::Cancel : Out::Menu;
}

static Out depositScreen() {
    led.cash = L_BLINK;  // shutter open
    Screen s;
    s.title = tr("depTitle");
    s.text = tr("depSub");
    bool empty = notesCount(st.wallet) == 0;
    std::string w;
    for (int d : {10, 20, 50})
        if (st.wallet[d]) w += (w.empty() ? "" : "  ") + fmtWhole(d) + " × " + std::to_string(st.wallet[d]);
    s.body = {{empty ? tr("emptyWallet") : tr("depWallet") + "  " + w, empty ? ST_ERR : ST_DIM}};
    s.opts = {{'L', 4, tr("cancel")}, {'R', 4, tr("depInsert"), true, empty}};
    s.hint = "Choose \"" + tr("depInsert") + "\" to put all notes from your wallet into the slot.";
    Res r = run(s);
    if (r.kind != R_OPT) {
        led.cash = L_OFF;
        return Out::Cancel;
    }
    if (r.opt == 0) {
        led.cash = L_OFF;
        return Out::Menu;
    }
    Notes notes = st.wallet;
    for (int d : DENOMS) st.wallet[d] = 0;
    save();
    led.cash = L_OFF;
    bell();
    busy(spinnerScreen(tr("counting"), tr("wait")), 1000 + notesCount(notes) * 75, cashArt(notes));

    Cents total = notesTotal(notes) * 100LL;
    Screen c;
    c.title = tr("depCounted");
    for (int d : {10, 20, 50})
        if (notes[d]) c.body.push_back({padR(fmtWhole(d), 7) + " × " + padR(std::to_string(notes[d]), 4) + padL(fmt(d * notes[d] * 100LL), 12)});
    c.body.push_back({fmt(total), ST_BIG});
    c.opts = {{'R', 3, tr("depConfirm"), true}, {'R', 4, tr("depReturn")}};
    c.noTimeout = true;
    Res cr = run(c);
    if (cr.kind != R_OPT) return returnDeposit(notes, true);
    if (cr.opt == 1) return returnDeposit(notes, false);

    Account& a = acct();
    busy(spinnerScreen(tr("processing"), tr("wait")), 1400);
    long long trn = nextTrn();
    a.checking += total;
    a.tx.push_back({nowMs(), total, tr("txDeposit")});
    for (int d : DENOMS) st.cassettes[d] = std::min(st.capacity[d], st.cassettes[d] + notes[d]);  // recycled
    save();
    jr("TRN " + std::to_string(trn) + " DEP EUR " + fmtPlain(total) + " NOTES " + notesStr(notes) + " RECYCLED");
    Screen d;
    d.title = tr("depDone");
    d.body = {kv(tr("trAmount"), fmt(total)), kv(tr("newBal"), fmt(a.checking))};
    d.opts = {{'R', 3, tr("print")}, {'R', 4, tr("cont"), true}};
    Res dr = run(d);
    if (dr.kind != R_OPT) return Out::Cancel;
    if (dr.opt == 0)
        printReceipt(receipt("CASH DEPOSIT",
                             {{"TRN", std::to_string(trn)}, {"NOTES", notesStr(notes)}, {"AMOUNT", "EUR " + fmtPlain(total)}, {"BALANCE", "EUR " + fmtPlain(a.checking)}},
                             a));
    return Out::Another;
}

// ---- transfer ----

enum TrKind { TR_TO_SAV, TR_FROM_SAV, TR_OTHER };

static std::string shortName(const std::string& holder) {
    size_t sp = holder.find(' ');
    return sp == std::string::npos ? holder : holder.substr(0, 1) + ". " + holder.substr(sp + 1);
}

static Out doTransfer(TrKind kind, int to, Cents cents, const std::string& toLabel) {
    Account& a = acct();
    busy(spinnerScreen(tr("processing"), tr("wait")), 1500);
    long long trn = nextTrn();
    if (kind == TR_TO_SAV) {
        a.checking -= cents;
        a.savings += cents;
        a.tx.push_back({nowMs(), -cents, tr("txToSav")});
    } else if (kind == TR_FROM_SAV) {
        a.savings -= cents;
        a.checking += cents;
        a.tx.push_back({nowMs(), cents, tr("txFromSav")});
    } else {
        Account& b = st.accounts[static_cast<size_t>(to)];
        a.checking -= cents;
        b.checking += cents;
        a.tx.push_back({nowMs(), -cents, tr("txTo", b.holder)});
        b.tx.push_back({nowMs(), cents, tr("txFrom", a.holder)});
    }
    save();
    std::string dest = kind == TR_OTHER ? st.accounts[static_cast<size_t>(to)].acctNo : kind == TR_TO_SAV ? "SAVINGS" : "CURRENT";
    jr("TRN " + std::to_string(trn) + " XFER EUR " + fmtPlain(cents) + (kind == TR_OTHER ? " TO " : " ") + (kind == TR_OTHER ? dest : kind == TR_TO_SAV ? "TOSAV" : "FROMSAV"));
    bell();
    Screen s;
    s.title = tr("trDone");
    s.body = {kv(tr("trTo"), toLabel), kv(tr("trAmount"), fmt(cents)), kv(tr("checkingAcc"), fmt(a.checking))};
    s.opts = {{'R', 3, tr("print")}, {'R', 4, tr("cont"), true}};
    Res r = run(s);
    if (r.kind != R_OPT) return Out::Cancel;
    if (r.opt == 0)
        printReceipt(receipt("TRANSFER",
                             {{"TRN", std::to_string(trn)}, {"TO", dest}, {"AMOUNT", "EUR " + fmtPlain(cents)}, {"BALANCE", "EUR " + fmtPlain(a.checking)}},
                             a));
    return Out::Another;
}

static Out transferMenu();

static Out transferAmount(TrKind kind, int to) {
    std::string msg;
    for (;;) {
        Screen s;
        s.title = tr("trAmt");
        s.text = msg.empty() ? tr("trAmtSub") : msg;
        s.err = !msg.empty();
        s.field = F_CENTS;
        s.fmax = 7;
        s.opts = {{'L', 4, tr("back"), false, false, 'b'}, {'R', 4, tr("confirm"), true, false, '\n'}};
        Res r = run(s);
        if (r.kind == R_OPT) return transferMenu();
        if (r.kind != R_ENTER) return Out::Cancel;
        Cents cents = r.val.empty() ? 0 : std::stoll(r.val);
        Account& a = acct();
        Cents src = kind == TR_FROM_SAV ? a.savings : a.checking;
        if (!cents) msg = tr("zero");
        else if (cents > src) msg = tr("insufficient", fmt(src));
        else {
            std::string from = kind == TR_FROM_SAV ? tr("savingsAcc") : tr("checkingAcc");
            std::string toLabel = kind == TR_TO_SAV ? tr("savingsAcc") : kind == TR_FROM_SAV ? tr("checkingAcc")
                : st.accounts[static_cast<size_t>(to)].acctNo + " · " + shortName(st.accounts[static_cast<size_t>(to)].holder);
            Screen c;
            c.title = tr("trConfirm");
            c.body = {kv(tr("trFrom"), from), kv(tr("trTo"), toLabel), kv(tr("trAmount"), fmt(cents))};
            c.opts = {{'R', 3, tr("confirm"), true}, {'R', 4, tr("cancel")}};
            Res cr = run(c);
            if (cr.kind != R_OPT) return Out::Cancel;
            if (cr.opt == 1) return Out::Menu;
            return doTransfer(kind, to, cents, toLabel);
        }
        bell();
    }
}

static Out transferAccount() {
    std::string msg;
    for (;;) {
        Screen s;
        s.title = tr("trAcct");
        s.text = msg.empty() ? tr("trAcctSub") : msg;
        s.err = !msg.empty();
        s.field = F_DIGITS;
        s.fmax = 7;
        s.opts = {{'L', 4, tr("back"), false, false, 'b'}, {'R', 4, tr("confirm"), true, false, '\n'}};
        for (const Account& o : st.accounts)
            if (&o != &acct()) {
                s.hint = "Try account " + o.acctNo + " (" + o.holder + ").";
                break;
            }
        Res r = run(s);
        if (r.kind == R_OPT) return transferMenu();
        if (r.kind != R_ENTER) return Out::Cancel;
        int to = -1;
        for (size_t i = 0; i < st.accounts.size(); ++i)
            if (st.accounts[i].acctNo == r.val) to = static_cast<int>(i);
        if (to < 0) msg = tr("trBadAcct");
        else if (to == cur) msg = tr("trOwn");
        else return transferAmount(TR_OTHER, to);
        bell();
    }
}

static Out transferMenu() {
    Account& a = acct();
    Screen s;
    s.title = tr("trTitle");
    s.opts = {{'L', 1, tr("trToSav"), false, a.checking <= 0},
              {'L', 2, tr("trFromSav"), false, a.savings <= 0},
              {'L', 3, tr("trOther")},
              {'R', 4, tr("back")}};
    Res r = run(s);
    if (r.kind != R_OPT) return Out::Cancel;
    switch (r.opt) {
        case 0: return transferAmount(TR_TO_SAV, -1);
        case 1: return transferAmount(TR_FROM_SAV, -1);
        case 2: return transferAccount();
        default: return Out::Menu;
    }
}

// ---- statement and PIN change ----

static Out statementScreen() {
    Account& a = acct();
    std::vector<Tx> rows(a.tx.end() - std::min<long>(7, static_cast<long>(a.tx.size())), a.tx.end());
    std::reverse(rows.begin(), rows.end());
    auto dfmt = [](long long t) { return strftimeMs(t, g_de ? "%d.%m" : "%d/%m"); };
    jr("MINI STATEMENT");
    Screen s;
    s.title = tr("stmtTitle");
    if (rows.empty()) s.body = {{tr("noTx"), ST_DIM}};
    for (const Tx& t : rows) {
        std::string amt = fmt(t.amt);
        s.body.push_back({sty(th.dim, dfmt(t.t)) + "  " + padR(cut(t.desc, 30), 30) + padL(t.amt < 0 ? amt : sty(th.ok, amt), 13)});
    }
    s.opts = {{'L', 4, tr("back")}, {'R', 4, tr("print")}};
    Res r = run(s);
    if (r.kind != R_OPT) return Out::Cancel;
    if (r.opt == 0) return Out::Menu;
    std::vector<std::pair<std::string, std::string>> rr;
    for (const Tx& t : rows) rr.push_back({dfmt(t.t) + " " + upper(cut(t.desc, 13)), fmtPlain(t.amt)});
    rr.push_back({"BALANCE", "EUR " + fmtPlain(a.checking)});
    printReceipt(receipt("MINI STATEMENT", rr, a));
    return Out::Another;
}

static Out changePin() {
    std::string msg;
    for (;;) {
        Screen s;
        s.title = tr("newPin");
        s.text = msg.empty() ? tr("newPinSub") : msg;
        s.err = !msg.empty();
        s.field = F_PIN;
        s.fmax = 4;
        s.opts = {{'L', 4, tr("back"), false, false, 'b'}};
        Res r = run(s);
        if (r.kind == R_OPT) return Out::More;
        if (r.kind != R_ENTER) return Out::Cancel;
        bell();
        if (r.val.size() != 4) {
            msg = tr("newPinSub");
            continue;
        }
        if (r.val == acct().pin) {
            msg = tr("pinSame");
            continue;
        }
        Screen t;
        t.title = tr("repeatPin");
        t.field = F_PIN;
        t.fmax = 4;
        t.opts = {{'L', 4, tr("back"), false, false, 'b'}};
        Res r2 = run(t);
        if (r2.kind == R_OPT) return Out::More;
        if (r2.kind != R_ENTER) return Out::Cancel;
        if (r2.val != r.val) {
            msg = tr("pinMismatch");
            continue;
        }
        busy(spinnerScreen(tr("processing")), 1400);
        acct().pin = r.val;
        save();
        jr("PIN CHANGED");
        Screen d;
        d.title = tr("pinChanged");
        d.body = {{"✔", ST_OK}};
        d.opts = {{'R', 4, tr("cont"), true}};
        Res dr = run(d);
        return dr.kind == R_OPT ? Out::Another : Out::Cancel;
    }
}

// ---- menus ----

static Out moreMenu() {
    Screen s;
    s.title = tr("more");
    s.opts = {{'L', 1, tr("statement")}, {'L', 2, tr("changePin")}, {'R', 3, tr("returnCard")}, {'R', 4, tr("back")}};
    Res r = run(s);
    if (r.kind != R_OPT) return Out::Cancel;
    switch (r.opt) {
        case 0: return statementScreen();
        case 1: return changePin();
        case 2: return Out::End;
        default: return Out::Menu;
    }
}

static Out menu() {
    bool cashOk = cashAvailable();
    Account& a = acct();
    Screen s;
    s.title = tr("menu");
    s.text = a.holder + " · ****" + last4(a);
    s.opts = {{'L', 1, fmt(5000), false, !cashOk}, {'L', 2, fmt(10000), false, !cashOk}, {'L', 3, fmt(20000), false, !cashOk},
              {'L', 4, tr("other"), false, !cashOk}, {'R', 1, tr("balance")}, {'R', 2, tr("deposit")},
              {'R', 3, tr("transfer")}, {'R', 4, tr("more")}};
    s.hint = "Pick a service with its number. Esc ends the session and returns your card.";
    Res r = run(s);
    if (r.kind != R_OPT) return Out::Cancel;
    switch (r.opt) {
        case 0: return askReceiptThenWithdraw(50);
        case 1: return askReceiptThenWithdraw(100);
        case 2: return askReceiptThenWithdraw(200);
        case 3: return amountScreen();
        case 4: return balanceScreen();
        case 5: return depositScreen();
        case 6: return transferMenu();
        default: return moreMenu();
    }
}

enum PinResult { PIN_OK, PIN_CAPTURED, PIN_CANCEL };

static PinResult pinEntry() {
    std::string msg;
    for (;;) {
        Screen s;
        s.title = tr("pinTitle");
        s.text = msg.empty() ? tr("pinSub") : msg;
        s.err = !msg.empty();
        s.field = F_PIN;
        s.fmax = 6;
        s.hint = "Type the PIN and press Enter. Wrong 3 times and the card is kept.";
        Res r = run(s);
        if (r.kind != R_ENTER) return PIN_CANCEL;
        if (r.val.size() < 4) {
            bell();
            msg = tr("pinShort");
            continue;
        }
        Account& a = acct();
        busy(spinnerScreen(tr("checkingPin"), tr("wait")), 1300);
        if (r.val == a.pin) {
            a.tries = 0;
            save();
            jr("PIN OK");
            return PIN_OK;
        }
        a.tries += 1;
        save();
        jr("PIN INVALID (" + std::to_string(a.tries) + "/3)");
        bell();
        if (a.tries >= 3) {
            a.captured = true;
            a.tries = 0;
            save();
            jr("CARD CAPTURED ****" + last4(a) + " - PIN TRIES EXCEEDED");
            info(tr("retained"), tr("retainedSub"), 5000, "The card is gone. Use Z (reset demo) on the start screen to get it back.");
            finish(true);
            return PIN_CAPTURED;
        }
        int left = 3 - a.tries;
        msg = left == 1 ? tr("wrongPin1") : tr("wrongPinN", std::to_string(left));
    }
}

static bool cardExpired(const Account& a) {
    int mm = std::stoi(a.exp.substr(0, 2)), yy = 2000 + std::stoi(a.exp.substr(3, 2));
    std::tm now = localTm(nowMs());
    int y = now.tm_year + 1900, m = now.tm_mon + 1;
    return y > yy || (y == yy && m > mm);  // valid through the end of the expiry month
}

static void session(int idx) {
    cur = idx;
    g_de = false;
    Account& a = acct();
    led.card = L_OFF;
    animateCard(a, true);
    jr("CARD INSERTED ****" + last4(a));
    busy(spinnerScreen(tr("reading"), tr("dontRemove")), 1500);
    if (cardExpired(a)) {
        jr("CARD EXPIRED " + a.exp + " - EJECT");
        bell();
        info(tr("expired"), tr("expiredSub"), 2600);
        returnCard(tr("expired"));
        finish();
        return;
    }

    Screen lang;
    lang.title = "Please select your language";
    lang.text = "Bitte wählen Sie Ihre Sprache";
    lang.opts = {{'R', 3, "English"}, {'R', 4, "Deutsch"}};
    lang.hint = "Choose a language with its number.";
    Res lr = run(lang);
    if (lr.kind != R_OPT) {
        endSession(false);
        return;
    }
    g_de = lr.opt == 1;
    jr(g_de ? "LANGUAGE DE" : "LANGUAGE EN");

    PinResult p = pinEntry();
    if (p == PIN_CAPTURED) return;
    if (p == PIN_CANCEL) {
        endSession(false);
        return;
    }

    Out o = Out::Menu;
    for (;;) {
        switch (o) {
            case Out::Menu: o = menu(); break;
            case Out::More: o = moreMenu(); break;
            case Out::Another: o = another(); break;
            case Out::End: endSession(true); return;
            case Out::Cancel: endSession(false); return;
            case Out::Finished: return;
        }
    }
}

// ============================================================================
//  Start screen, wallet and operator views
// ============================================================================

static std::vector<std::string> walletPanel() {
    std::vector<std::string> v;
    v.push_back("");
    v.push_back("  " + paint("\x1b[1m", "YOUR WALLET") + paint("\x1b[38;5;245m", "   press 1-3 to insert a card"));
    for (size_t i = 0; i < st.accounts.size(); ++i) {
        const Account& a = st.accounts[i];
        std::string chip = g_color ? "\x1b[1;38;5;231;48;5;" + std::to_string(cardColour(a)) + "m" : "";
        std::string card = " ▣ " + a.pan + "  " + padR(a.holder, 12) + " " + a.exp + " ";
        std::string note = "PIN " + a.pin + " · Acct " + a.acctNo + (a.note.empty() ? "" : " · " + a.note);
        if (a.captured)
            v.push_back("   " + paint("\x1b[38;5;240m", "[-]" + card) + " " + paint("\x1b[1;38;5;203m", "RETAINED BY ATM"));
        else
            v.push_back("   " + paint("\x1b[1m", "[" + std::to_string(i + 1) + "]") + paint(chip, card) + " " + paint("\x1b[38;5;16;48;5;229m", " ✎ " + note + " "));
    }
    std::string cash;
    for (int d : {10, 20, 50})
        if (st.wallet[d]) cash += "€" + std::to_string(d) + " × " + std::to_string(st.wallet[d]) + "   ";
    bool saved = g_de;
    g_de = false;
    v.push_back("   Cash  " + (cash.empty() ? paint("\x1b[38;5;245m", "no notes   ") : cash) + "total " + paint("\x1b[1m", fmt(notesTotal(st.wallet) * 100LL)) +
                "      Receipts " + std::to_string(st.receipts.size()));
    g_de = saved;
    std::string cas = "   " + paint("\x1b[1m", "ATM  ");
    for (int d : {10, 20, 50}) {
        int n = st.cassettes[d], cap = st.capacity[d];
        int fill = cap ? (n * 10 + cap - 1) / cap : 0;
        std::string col = n == 0 ? "\x1b[38;5;203m" : n * 100 / cap < 15 ? "\x1b[38;5;214m" : "\x1b[38;5;71m";
        cas += "€" + std::to_string(d) + " " + paint(col, rep("█", fill)) + paint("\x1b[38;5;238m", rep("░", 10 - fill)) + " " + padR(std::to_string(n), 4);
    }
    cas += "bin " + std::to_string(st.retractBin);
    v.push_back(cas);
    return v;
}

static void pager(const std::string& title, const std::vector<std::string>& lines) {
    std::string o = g_tty ? "\x1b[H\x1b[2J" : "\n";
    o += "  " + paint("\x1b[1m", title) + "\n  " + paint("\x1b[38;5;245m", rep("─", 66)) + "\n";
    for (const std::string& l : lines) o += "  " + l + "\n";
    o += "\n  " + paint("\x1b[38;5;245m", "Press Enter to return") + "\n";
    std::fwrite(o.data(), 1, o.size(), stdout);
    std::fflush(stdout);
    if (in.keyMode()) {
        for (;;) {
            Key k = in.next(-1);
            if (k.k == K::Enter || k.k == K::Esc || k.k == K::Eof || k.k == K::Char) break;
        }
    } else {
        std::string line;
        in.nextLine(-1, line);
    }
    if (g_tty) std::fputs("\x1b[2J", stdout);
}

static void journalView() {
    size_t from = st.journal.size() > 40 ? st.journal.size() - 40 : 0;
    std::vector<std::string> v(st.journal.begin() + static_cast<long>(from), st.journal.end());
    if (v.empty()) v.push_back("Electronic journal is empty.");
    v.push_back("");
    v.push_back(paint("\x1b[38;5;245m", "TRN counter " + std::to_string(st.trn) + " · retract bin " + std::to_string(st.retractBin) + " notes"));
    pager("ELECTRONIC JOURNAL  ·  ATM " + std::string(ATM_ID) + "  (last 40 entries)", v);
}

static void receiptsView() {
    if (st.receipts.empty()) {
        pager("RECEIPTS", {"Receipts you take appear here."});
        return;
    }
    std::vector<std::string> v;
    size_t n = std::min<size_t>(9, st.receipts.size());
    for (size_t i = 0; i < n; ++i) {
        const Receipt& r = st.receipts[st.receipts.size() - 1 - i];
        v.push_back("[" + std::to_string(i + 1) + "] " + strftimeMs(r.t, "%d.%m %H:%M") + "  " + (r.lines.size() > 7 ? r.lines[7] : "Receipt"));
    }
    v.push_back("");
    v.push_back(paint("\x1b[38;5;245m", "Type a number to view a receipt, Enter to return."));
    std::string o = g_tty ? "\x1b[H\x1b[2J" : "\n";
    o += "  " + paint("\x1b[1m", "RECEIPTS IN YOUR WALLET") + "\n\n";
    for (const std::string& l : v) o += "  " + l + "\n";
    std::fwrite(o.data(), 1, o.size(), stdout);
    std::fflush(stdout);
    int pick = -1;
    if (in.keyMode()) {
        Key k = in.next(-1);
        if (k.k == K::Char && k.c >= '1' && k.c <= '9') pick = k.c - '1';
    } else {
        std::string line;
        if (in.nextLine(-1, line) == 1 && trim(line).size() == 1 && allDigits(trim(line))) pick = trim(line)[0] - '1';
    }
    if (g_tty) std::fputs("\x1b[2J", stdout);
    if (pick >= 0 && static_cast<size_t>(pick) < n) pager("RECEIPT", paperArt(st.receipts[st.receipts.size() - 1 - static_cast<size_t>(pick)].lines, 99));
}

static bool confirmReset() {
    std::string o = g_tty ? "\x1b[H\x1b[2J" : "\n";
    o += "  " + paint("\x1b[1m", "RESET DEMO") + "\n\n  Restores all cards, balances, wallet cash and cassettes.\n\n  " +
         paint("\x1b[38;5;245m", "Press Y to reset, any other key to keep the current state.") + "\n";
    std::fwrite(o.data(), 1, o.size(), stdout);
    std::fflush(stdout);
    bool yes = false;
    if (in.keyMode()) {
        Key k = in.next(-1);
        yes = k.k == K::Char && (k.c == 'y' || k.c == 'Y');
    } else {
        std::string line;
        yes = in.nextLine(-1, line) == 1 && (trim(line) == "y" || trim(line) == "Y");
    }
    if (g_tty) std::fputs("\x1b[2J", stdout);
    return yes;
}

static void idleLoop() {
    bool flip = false;
    for (;;) {
        if (g_eof) return;
        cur = -1;
        g_de = flip;
        bool cashOk = cashAvailable();
        Screen s;
        s.title = tr("welcome");
        s.text = tr("insert");
        s.body = {{""},
                  {"╭────────────────╮", ST_DIM},
                  {"│ ▣▣  ══════════ │", ST_DIM},
                  {"╰───────┬────────╯", ST_DIM},
                  {"▼", ST_TITLE},
                  {"━━━━━━━━━━━━━━━━━━━━━━", ST_TITLE},
                  {""},
                  {cashOk ? tr("insertSub") : tr("noCash"), cashOk ? ST_DIM : ST_ERR}};
        s.hint = "Insert a card from your wallet (1-3). The PIN is on the sticky note.";
        led.card = L_BLINK;
        g_de = false;
        render(s, "", walletPanel());
        std::string legend = "  " + paint("\x1b[38;5;245m", "1-3 insert card · R receipts · J journal · Z reset demo · Q quit");
        std::printf("%s%s\n", legend.c_str(), g_tty ? "\x1b[K" : "");
        std::fflush(stdout);

        char c = 0;
        if (in.keyMode()) {
            Key k = in.next(g_tty ? 4000 : -1);
            if (k.k == K::Timeout) {
                flip = !flip;  // alternate English / German like a real attract screen
                continue;
            }
            if (k.k == K::Eof) return;
            if (k.k == K::Esc) continue;
            if (k.k == K::Char) c = static_cast<char>(std::tolower(k.c));
        } else {
            std::string line;
            if (in.nextLine(-1, line) != 1) return;
            echoInput(s, line);
            line = trim(line);
            if (line.size() != 1) {
                if (!line.empty()) g_notice = "Unknown command \"" + line + "\".";
                continue;
            }
            c = static_cast<char>(std::tolower(line[0]));
        }
        flip = false;
        if (c >= '1' && c <= '3') {
            size_t i = static_cast<size_t>(c - '1');
            if (st.accounts[i].captured) {
                g_notice = "That card was retained by the ATM. Press Z to reset the demo.";
                bell();
                continue;
            }
            led.card = L_OFF;
            session(static_cast<int>(i));
            g_de = false;
        } else if (c == 'j') {
            journalView();
        } else if (c == 'r') {
            receiptsView();
        } else if (c == 'z') {
            if (confirmReset()) {
                st = defaults();
                save();
                jr("DEMO RESET - CASSETTES REPLENISHED");
                g_notice.clear();
            }
        } else if (c == 'q') {
            return;
        } else {
            g_notice = "Unknown key. Use 1-3, R, J, Z or Q.";
            bell();
        }
    }
}

// ============================================================================
//  Autoplay tour
// ============================================================================

static void buildTour() {
    auto step = [](const std::string& keys, int delay = 1600) {
        bool first = true;
        for (char c : keys) {
            in.script.push_back({first ? delay : 260, c});
            first = false;
        }
    };
    step("1", 2500);       // insert Alex's card
    step("1");             // English
    step("1234\n", 1400);  // PIN
    step("5");             // balance
    step("1");             // print a receipt
    step("\n", 2200);      // take receipt
    step("1");             // another service: yes
    step("7");             // transfer
    step("1");             // current -> savings
    step("2500\n");        // EUR 25.00
    step("1");             // confirm
    step("2");             // continue
    step("1");             // another service: yes
    step("8");             // more services
    step("1");             // mini statement
    step("1", 3500);       // back
    step("2");             // EUR 100 fast cash
    step("1");             // with receipt
    step("\n", 2000);      // take card
    step("\n", 2000);      // take cash
    step("\n", 2200);      // take receipt
    step("3", 3000);       // expired card
    step("\n", 2000);      // take it back
    step("2", 3000);       // Sam's card
    step("2");             // Deutsch
    step("0000\n");        // wrong PIN
    step("2580\n", 2500);  // right PIN
    step("6");             // deposit
    step("2");             // insert wallet notes
    step("1", 2500);       // confirm deposit
    step("2", 2500);       // continue
    step("2");             // no further service
    step("\n", 2000);      // take card
    step("j", 3000);       // electronic journal
    step("\n", 5000);
    step("q", 2500);
}

// ============================================================================
//  main
// ============================================================================

static void usage(FILE* f) {
    std::fprintf(f,
                 "Brainhub24 ATM Simulator %s (CLI)\n\n"
                 "Usage: atm [options]\n\n"
                 "  --theme NAME     brand (default), classic, blue, amber, mono\n"
                 "  --no-color       plain output (also: NO_COLOR environment variable)\n"
                 "  --sound          terminal bell for alerts and errors\n"
                 "  --timeout SEC    inactivity timeout, default 30, 0 = off\n"
                 "  --fast           no animations or delays (for scripts and tests)\n"
                 "  --plain          line mode: type your choice and press Enter, screens scroll\n"
                 "  --autoplay       run a guided tour on a fresh demo bank (any key stops it)\n"
                 "  --state FILE     where the bank state is kept\n"
                 "                   (default $ATM_STATE_FILE, else ~/.local/share/brainhub24-atm/state.tsv\n"
                 "                   or %%APPDATA%%\\brainhub24-atm\\state.tsv on Windows)\n"
                 "  --reset          restore the demo bank before starting\n"
                 "  --version, --help\n\n"
                 "Demo cards (also shown on the start screen):\n"
                 "  1  ALEX MORGAN  PIN 1234   normal account with savings\n"
                 "  2  SAM RIVERA   PIN 2580   low balance\n"
                 "  3  JO PATEL     PIN 1111   expired card\n\n"
                 "Keys: digits, Enter, Backspace, Esc or X = cancel. Piped input is read one line per screen.\n",
                 VERSION);
}

int main(int argc, char** argv) {
    plat::setupConsole();
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "atm: %s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--theme") opt.theme = value();
        else if (a == "--no-color" || a == "--no-colour") opt.color = false;
        else if (a == "--sound") opt.sound = true;
        else if (a == "--fast") opt.fast = true;
        else if (a == "--autoplay" || a == "--demo") opt.autoplay = true;
        else if (a == "--reset") opt.reset = true;
        else if (a == "--plain") opt.plain = true;
        else if (a == "--state") opt.statePath = value();
        else if (a == "--timeout") {
            std::string v = value();
            if (!allDigits(v) || v.size() > 4) {
                std::fprintf(stderr, "atm: --timeout expects seconds (0-9999)\n");
                return 2;
            }
            opt.timeout = std::stoi(v);
        } else if (a == "--help" || a == "-h") {
            usage(stdout);
            return 0;
        } else if (a == "--version") {
            std::printf("Brainhub24 ATM Simulator %s\n", VERSION);
            return 0;
        } else {
            std::fprintf(stderr, "atm: unknown option '%s'\n\n", a.c_str());
            usage(stderr);
            return 2;
        }
    }
    if (!setTheme(opt.theme)) {
        std::fprintf(stderr, "atm: unknown theme '%s' (brand, classic, blue, amber, mono)\n", opt.theme.c_str());
        return 2;
    }

    g_tty = !opt.plain && plat::stdinTty() && plat::stdoutTty();
    const char* term = std::getenv("TERM");
    g_color = opt.color && plat::stdoutTty() && !std::getenv("NO_COLOR") && !(term && std::string(term) == "dumb");
#ifdef _WIN32
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#else
    struct sigaction sa {};
    sa.sa_handler = onSignal;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
#endif
    if (opt.statePath.empty()) opt.statePath = defaultStatePath();

    if (opt.autoplay) {
        g_persist = false;  // the tour never touches your saved bank
        st = defaults();
        buildTour();
    } else if (opt.reset || !load()) {
        st = defaults();
        save();
    }
    for (Account& a : st.accounts) rollDay(a);
    if (st.journal.empty()) jr(std::string("ATM ") + ATM_ID + " IN SERVICE");
    jr("ATM SOFTWARE " + std::string(VERSION) + " STARTED");

    termEnter();
    idleLoop();
    jr("ATM SHUTDOWN");
    termLeave();

    std::printf("\nThank you for using the Brainhub24 ATM Simulator.\n%s\n", COPYRIGHT);
    if (g_saveFailed) std::fprintf(stderr, "atm: warning: could not save state to %s\n", opt.statePath.c_str());
    return 0;
}
