# Personalized Gift Shop

A small local web app for ordering customized gifts (mugs, frames, teddy bears, and similar items).

One C++ program is the website **and** the backend. The browser only displays pages. C++ handles login, products, order validation, prices, saving orders, and the admin dashboard.

Open **http://localhost:8080** after you start the program. Nothing is deployed to the internet; it only listens on your own computer.

## What you can do

**Customer**

1. Register or log in
2. Browse gifts
3. Personalize an item: printed message (max 30 characters), design style, optional photo, quantity
4. Place an order with name, phone, and address
5. View your own orders on **My Orders**

**Admin** (`admin@giftshop.lk` / `admin123`)

- Dashboard: order count, customers, sales
- Add, edit, or hide products
- View all orders and update status: Pending → Processing → Shipped → Delivered (or Cancelled)
- Sales report for today, this week, or this month

Prices are in Sri Lankan rupees (Rs.). The server always calculates `unit price × quantity` from the database. The total shown in the browser is not trusted.

## How it is built

```
Browser (HTML / CSS / JavaScript in public/)
    → HTTP + JSON APIs
        → C++ server (src/main.cpp)
            → SQLite file (data/giftshop.db)
```

| Part | Technology |
|---|---|
| Server and business rules | C++17 + cpp-httplib |
| Database | SQLite (`data/giftshop.db`, created on first run) |
| JSON | nlohmann/json |
| Passwords | salted SHA-256 |
| Pages | `public/` (HTML, CSS, JavaScript) |

All libraries are already in `third_party/`. You do not need npm, PHP, or a separate database server.

On first run the program creates:

- the admin account
- six sample products
- the `data/` folder (database + uploaded photos)

Login uses an `sid` cookie stored in memory. If you stop the program, everyone is logged out.

## Run on Linux / macOS

You need a C/C++ compiler (`cc` and `c++`, for example `g++`).

From the project folder:

```sh
./build.sh
./build/giftshop
```

The first build compiles SQLite (`third_party/sqlite3.c`). That file is large, so it can take **30–60 seconds**. A warning about `strrchr` / `const` is normal and does **not** mean the build failed. Wait until you see:

```text
Built. Run:  ./build/giftshop   then open http://localhost:8080
```

Then start the program. When it is ready:

```text
Gift shop running at http://localhost:8080  (press Ctrl+C to stop)
```

Open **http://localhost:8080** in your browser. Leave the terminal open while you use the shop. Stop the server with **Ctrl+C**.

If you already built once, you can skip `./build.sh` and just run `./build/giftshop`.

You must start the program from the **project folder** so it can find `public/`.

### CMake (optional)

```sh
cmake -S . -B build
cmake --build build
./build/giftshop
```

## Run in Visual Studio 2022 (Windows)

1. Install the **Desktop development with C++** workload (includes CMake).
2. **File → Open → Folder** and choose the `GiftShop` folder.
3. Wait for “CMake generation finished”, then pick **giftshop.exe** in the green start-button dropdown.
4. Press **F5**. A console window should print `Gift shop running at http://localhost:8080`.
5. Open **http://localhost:8080** in Chrome.

If Windows Firewall asks, click Allow. The server only listens on `127.0.0.1` (this PC). Visual Studio already runs the program from the project folder, so it can find `public/`.

## Logins

| Role | How to sign in |
|---|---|
| Admin | `admin@giftshop.lk` / `admin123` |
| Customer | **Login → Register** and create an account |

Change the admin password after the first run (see [Reset and customize](#reset-and-customize)).

## Pages

| URL | Who | What it is |
|---|---|---|
| `/` | Anyone | Home: banner and gift collection. **Select** opens the order form. |
| `/login.html` | Anyone | Login and register |
| `/order.html?id=1` | Logged-in customer | Personalize and place an order |
| `/myorders.html` | Customer | That customer’s order history |
| `/admin.html` | Admin only | Dashboard, products, orders, sales report |

## Rules the C++ server enforces

- You must be logged in to place an order.
- Only the admin can open admin pages or change products, orders, and reports.
- Message: 1–30 characters. Design: from the allowed list. Quantity: 1–20.
- Phone must look valid. Address is required.
- Photo (optional): JPG, PNG, GIF, or WEBP, max 2 MB. Files are stored under `data/uploads/` with a random name.
- Product hide does not delete old orders (the product is marked inactive).
- Cancelled orders are left out of sales totals.

## Project layout

```text
src/main.cpp          Server, routes, validation, sessions
src/db.h              SQLite wrapper
src/sha256.h          Password hashing
public/               Website pages, app.js, style.css
data/                 Created on first run: giftshop.db + uploads/
third_party/          httplib.h, json.hpp, sqlite3.c / sqlite3.h
build.sh              Linux / macOS build
CMakeLists.txt        Visual Studio / CMake build
```

## Reset and customize

| What | Where |
|---|---|
| Shop name and tagline | Top of `public/app.js` |
| Colours | Top of `public/style.css` |
| Port (default 8080) | `listen("127.0.0.1", 8080)` at the bottom of `src/main.cpp` |
| Wipe users, products, and orders | Stop the program, delete the `data/` folder, start again |
| Change the default admin password | Delete `data/`, change `admin123` in `initDb()` in `src/main.cpp`, run again |

## Troubleshooting

**The terminal shows a `sqlite3.c` warning and seems stuck.**  
That is a compiler warning, not a crash. Compiling SQLite takes a while. Do not press Ctrl+C. Wait for `Built. Run: ./build/giftshop`.

**`Cannot find the 'public' folder`.**  
Run `./build/giftshop` from the project folder (the folder that contains `public/`), not from `build/`.

**`Could not start server on port 8080`.**  
Something else is already using 8080 (often a previous `giftshop` still running). Stop it with Ctrl+C, or close that terminal, then start again.

**I rebuilt but the shop looks the same.**  
The running process is the old binary. Stop it, then start `./build/giftshop` again.
