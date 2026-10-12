## The ATM Simulator
The ATM Simulator is a command-line based program written in C++ that simulates an Automated Teller Machine (ATM) used for financial transactions. This program allows users to deposit, withdraw, check their account balance, transfer money and print receipts, with the same flow and rules as a real cash machine.

## Quick start (CLI)

```sh
g++ -std=c++17 -O2 -Wall -Wextra ATM.cpp -o atm
./atm               # interactive ATM in your terminal
./atm --autoplay    # watch a guided tour (any key stops it)
./atm --help        # all options
```

The CLI behaves like the GUI demo in [`demo/index.html`](demo/index.html), drawn as an ATM in the terminal:

- **Wallet with three demo cards.** Press `1`-`3` on the start screen to insert one. The PIN is on the sticky note next to each card.

  | Key | Card holder | PIN | Note |
  |-----|-------------|-----|------|
  | 1 | Alex Morgan | 1234 | normal account with savings |
  | 2 | Sam Rivera | 2580 | low balance |
  | 3 | Jo Patel | 1111 | expired card |

- **The full session:** card reader, English / Deutsch, masked PIN entry (card is kept after 3 wrong PINs, also across restarts), fast cash, other amount, balance, cash deposit from your wallet, transfers, mini statement and PIN change.
- **Behaves like a real machine:** the card comes back before the cash, cash not taken within 30 s is retracted and refunded, a card that is not taken is captured, and after 30 s without input it asks "Do you need more time?". Payouts depend on the notes left in the cassettes, there is a daily limit, and amounts must be multiples of €10.
- **Receipts** with ATM ID, transaction number and auth code (`R` on the start screen), an **electronic journal** (`J`) and cassette levels. `Z` resets the demo.
- **Keys:** digits, `Enter`, `Backspace`, `Esc` or `X` to cancel. Menu options are picked with the number shown next to them.
- **Themes:** `--theme brand|classic|blue|amber|mono`, `--no-color` (or `NO_COLOR`), `--sound` for the terminal bell.
- **State** is saved to `~/.local/share/brainhub24-atm/state.tsv`, not to the repository. Use `--state FILE` or `ATM_STATE_FILE` to put it somewhere else, and `--reset` to start over.
- **Scriptable:** with piped input, each line answers one screen. `--fast` turns off animations, for example:
  `printf '1\n1\n1234\n5\n2\n2\n\nq\n' | ./atm --fast --no-color --state /tmp/atm.tsv`

Interactive key handling and timeouts need a POSIX terminal (Linux, macOS, WSL). On Windows the program builds and falls back to line input without timeouts or colours.

**GUI version:** open [`demo/index.html`](demo/index.html) in a browser (card reader, PIN pad, cash dispenser, receipts, journal). The review of v2 and the plan for v3 are in [`docs/V3_PLAN.md`](docs/V3_PLAN.md).

This project is committed to fostering a welcoming and inclusive community. We do not tolerate any form of harassment or discrimination. We ask that all contributors and users respect each other and abide by our code of conduct.


```ruby
    ___  ________  ___     _____ ______  ___
   /   |/_  __/  |/  /    / ___//  _/  |/  /
  / /| | / / / /|_/ /_____\__ \ / // /|_/ / 
 / ___ |/ / / /  / /_____/__/ // // /  / /  
/_/  |_/_/ /_/  /_/     /____/___/_/  /_/   
                                                                                                               
```

### Insights
[Output and design | #Issue_3](https://github.com/Brainhub24/ATM/issues/3) DONE!

[sh: 1: color: not found | #Issue_2](https://github.com/Brainhub24/ATM/issues/2) DONE!

[Return statement #Issue_1](https://github.com/Brainhub24/ATM/issues/1) DONE!
