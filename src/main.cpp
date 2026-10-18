// Personalized Gift Shop - C++ web server (cpp-httplib + SQLite + nlohmann/json)
#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#endif
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <regex>
#include <set>
#include "httplib.h"
#include "json.hpp"
#include "db.h"
#include "sha256.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------- helpers
static std::string randomHex(size_t bytes) {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::mutex m;
    std::lock_guard<std::mutex> lk(m);
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < bytes; ++i) { unsigned v = gen() & 0xff; s += hex[v >> 4]; s += hex[v & 15]; }
    return s;
}
static std::string hashPassword(const std::string& pw, const std::string& salt) { return sha::sha256(salt + pw); }

static std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static size_t utf8Len(const std::string& s) {  // character count (not bytes), so Tamil/emoji count correctly
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}
static void sendJson(httplib::Response& res, int status, const json& j) {
    res.status = status;
    res.set_content(j.dump(), "application/json; charset=utf-8");
}
static void fail(httplib::Response& res, int status, const std::string& msg) { sendJson(res, status, {{"error", msg}}); }

// Form field works for both JSON-less multipart text fields and urlencoded fields.
static std::string field(const httplib::Request& req, const std::string& name) {
    if (req.has_file(name)) return req.get_file_value(name).content;
    return req.has_param(name) ? req.get_param_value(name) : "";
}

// ---------------------------------------------------------------- sessions
struct Session { long long id; std::string role, name; };
static std::map<std::string, Session> g_sessions;
static std::mutex g_sessMutex;

static bool currentUser(const httplib::Request& req, Session& out) {
    std::string cookie = req.get_header_value("Cookie");
    size_t p = cookie.find("sid=");
    if (p == std::string::npos) return false;
    size_t e = cookie.find(';', p);
    std::string tok = cookie.substr(p + 4, e == std::string::npos ? std::string::npos : e - p - 4);
    std::lock_guard<std::mutex> lk(g_sessMutex);
    auto it = g_sessions.find(tok);
    if (it == g_sessions.end()) return false;
    out = it->second;
    return true;
}
static bool requireLogin(const httplib::Request& req, httplib::Response& res, Session& s) {
    if (currentUser(req, s)) return true;
    fail(res, 401, "Please log in first."); return false;
}
static bool requireAdmin(const httplib::Request& req, httplib::Response& res, Session& s) {
    if (!requireLogin(req, res, s)) return false;
    if (s.role != "admin") { fail(res, 403, "Admin access only."); return false; }
    return true;
}

// ---------------------------------------------------------------- schema + seed
static void initDb(Db& db) {
    db.exec("CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL,"
            " email TEXT NOT NULL UNIQUE, phone TEXT, salt TEXT NOT NULL, pass_hash TEXT NOT NULL,"
            " role TEXT NOT NULL DEFAULT 'customer', created_at TEXT DEFAULT (datetime('now','localtime')))");
    db.exec("CREATE TABLE IF NOT EXISTS products(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL,"
            " price INTEGER NOT NULL CHECK(price > 0), emoji TEXT DEFAULT '🎁', description TEXT DEFAULT '',"
            " active INTEGER NOT NULL DEFAULT 1)");
    db.exec("CREATE TABLE IF NOT EXISTS orders(id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " user_id INTEGER NOT NULL REFERENCES users(id), product_id INTEGER NOT NULL REFERENCES products(id),"
            " product_name TEXT NOT NULL, unit_price INTEGER NOT NULL, quantity INTEGER NOT NULL, total INTEGER NOT NULL,"
            " message TEXT NOT NULL, design TEXT NOT NULL, photo TEXT, full_name TEXT NOT NULL, phone TEXT NOT NULL,"
            " address TEXT NOT NULL, delivery_address TEXT, status TEXT NOT NULL DEFAULT 'Pending',"
            " created_at TEXT DEFAULT (datetime('now','localtime')))");

    if (db.query("SELECT id FROM users WHERE role='admin'").empty()) {
        std::string salt = randomHex(8);
        db.exec("INSERT INTO users(name,email,phone,salt,pass_hash,role) VALUES(?,?,?,?,?,'admin')",
                {"Shop Admin", "admin@giftshop.lk", "0710000000", salt, hashPassword("admin123", salt)});
        std::cout << "Created default admin: admin@giftshop.lk / admin123\n";
    }
    if (db.query("SELECT id FROM products LIMIT 1").empty()) {
        struct P { const char *n, *e, *d; int price; };
        P items[] = {
            {"Personalized Mug", "☕", "Ceramic mug printed with your name, message or photo.", 1500},
            {"Photo Frame", "🖼️", "Wooden frame with a custom caption.", 2000},
            {"Teddy Bear", "🧸", "Soft teddy bear with a printed heart message.", 1800},
            {"Gift Box", "🎁", "Beautifully wrapped box with a personalized card.", 1200},
            {"Keychain", "🔑", "Heart-shaped keychain engraved with a name.", 800},
            {"Cushion", "💖", "Printed cushion for birthdays and anniversaries.", 1500}};
        for (auto& i : items)
            db.exec("INSERT INTO products(name,price,emoji,description) VALUES(?,?,?,?)",
                    {i.n, std::to_string(i.price), i.e, i.d});
    }
}

static const std::set<std::string> kStatuses = {"Pending", "Processing", "Shipped", "Delivered", "Cancelled"};
static const std::set<std::string> kDesigns = {"Floral", "Classic", "Modern", "Minimal", "Birthday", "Anniversary"};

static std::string periodFilter(const std::string& p) {
    if (p == "today") return " AND date(created_at)=date('now','localtime')";
    if (p == "week")  return " AND created_at >= datetime('now','localtime','-7 days')";
    if (p == "month") return " AND created_at >= date('now','localtime','start of month')";
    return "";
}

static fs::path findPublicDir() {
    for (const char* c : {"public", "../public", "../../public", "../../../public"})
        if (fs::exists(fs::path(c) / "index.html")) return fs::absolute(c);
    throw std::runtime_error("Cannot find the 'public' folder. Run the program from the project folder.");
}

// ---------------------------------------------------------------- main
int main() {
    try {
        fs::path pub = findPublicDir();
        fs::path dataDir = pub.parent_path() / "data";
        fs::create_directories(dataDir / "uploads");

        Db db((dataDir / "giftshop.db").string());
        initDb(db);

        httplib::Server svr;
        svr.set_payload_max_length(6 * 1024 * 1024);
        svr.set_mount_point("/", pub.string());
        svr.set_mount_point("/uploads", (dataDir / "uploads").string());
        svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
            try { if (ep) std::rethrow_exception(ep); } catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << "\n"; }
            fail(res, 500, "Something went wrong on the server.");
        });

        // ---------- auth
        svr.Post("/api/register", [&](const httplib::Request& req, httplib::Response& res) {
            json b = json::parse(req.body, nullptr, false);
            if (b.is_discarded()) return fail(res, 400, "Invalid request.");
            std::string name = trim(b.value("name", "")), email = trim(b.value("email", "")),
                        phone = trim(b.value("phone", "")), pw = b.value("password", "");
            if (name.empty() || utf8Len(name) > 60) return fail(res, 400, "Please enter your name (max 60 characters).");
            if (!std::regex_match(email, std::regex(R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)"))) return fail(res, 400, "Please enter a valid email.");
            if (pw.size() < 6) return fail(res, 400, "Password must be at least 6 characters.");
            if (!db.query("SELECT id FROM users WHERE lower(email)=lower(?)", {email}).empty())
                return fail(res, 409, "This email is already registered.");
            std::string salt = randomHex(8);
            db.exec("INSERT INTO users(name,email,phone,salt,pass_hash) VALUES(?,?,?,?,?)",
                    {name, email, phone, salt, hashPassword(pw, salt)});
            sendJson(res, 201, {{"ok", true}});
        });

        svr.Post("/api/login", [&](const httplib::Request& req, httplib::Response& res) {
            json b = json::parse(req.body, nullptr, false);
            if (b.is_discarded()) return fail(res, 400, "Invalid request.");
            auto r = db.query("SELECT * FROM users WHERE lower(email)=lower(?)", {trim(b.value("email", ""))});
            if (r.empty() || r[0]["pass_hash"] != hashPassword(b.value("password", ""), r[0]["salt"]))
                return fail(res, 401, "Incorrect email or password.");
            std::string tok = randomHex(24);
            { std::lock_guard<std::mutex> lk(g_sessMutex);
              g_sessions[tok] = {r[0]["id"], r[0]["role"], r[0]["name"]}; }
            res.set_header("Set-Cookie", "sid=" + tok + "; Path=/; HttpOnly; SameSite=Lax");
            sendJson(res, 200, {{"name", r[0]["name"]}, {"role", r[0]["role"]}});
        });

        svr.Post("/api/logout", [&](const httplib::Request& req, httplib::Response& res) {
            std::string c = req.get_header_value("Cookie");
            size_t p = c.find("sid=");
            if (p != std::string::npos) {
                size_t e = c.find(';', p);
                std::lock_guard<std::mutex> lk(g_sessMutex);
                g_sessions.erase(c.substr(p + 4, e == std::string::npos ? std::string::npos : e - p - 4));
            }
            res.set_header("Set-Cookie", "sid=; Path=/; Max-Age=0");
            sendJson(res, 200, {{"ok", true}});
        });

        svr.Get("/api/me", [&](const httplib::Request& req, httplib::Response& res) {
            Session s;
            if (!currentUser(req, s)) return sendJson(res, 200, {{"loggedIn", false}});
            auto r = db.query("SELECT name,email,phone FROM users WHERE id=?", {std::to_string(s.id)});
            sendJson(res, 200, {{"loggedIn", true}, {"name", s.name}, {"role", s.role},
                                {"email", r.empty() ? "" : r[0]["email"]}, {"phone", r.empty() ? json("") : r[0]["phone"]}});
        });

        // ---------- products
        svr.Get("/api/products", [&](const httplib::Request& req, httplib::Response& res) {
            Session s;
            bool all = req.has_param("all") && currentUser(req, s) && s.role == "admin";
            sendJson(res, 200, db.query(all ? "SELECT * FROM products ORDER BY id"
                                            : "SELECT * FROM products WHERE active=1 ORDER BY id"));
        });

        svr.Get(R"(/api/products/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
            auto r = db.query("SELECT * FROM products WHERE id=? AND active=1", {req.matches[1]});
            if (r.empty()) return fail(res, 404, "Product not found.");
            sendJson(res, 200, r[0]);
        });

        auto validateProduct = [](const json& b, std::string& err) {
            std::string n = trim(b.value("name", ""));
            if (n.empty() || utf8Len(n) > 60) { err = "Product name is required (max 60 characters)."; return false; }
            if (!b.contains("price") || !b["price"].is_number_integer() || b["price"].get<long long>() <= 0 ||
                b["price"].get<long long>() > 1000000) { err = "Price must be a whole number greater than 0."; return false; }
            return true;
        };

        svr.Post("/api/admin/products", [&](const httplib::Request& req, httplib::Response& res) {
            Session s; if (!requireAdmin(req, res, s)) return;
            json b = json::parse(req.body, nullptr, false); std::string err;
            if (b.is_discarded() || !validateProduct(b, err)) return fail(res, 400, err.empty() ? "Invalid request." : err);
            long long id = db.exec("INSERT INTO products(name,price,emoji,description) VALUES(?,?,?,?)",
                {trim(b["name"]), std::to_string(b["price"].get<long long>()),
                 trim(b.value("emoji", "🎁")).empty() ? "🎁" : trim(b.value("emoji", "🎁")), trim(b.value("description", ""))});
            sendJson(res, 201, {{"id", id}});
        });

        svr.Put(R"(/api/admin/products/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
            Session s; if (!requireAdmin(req, res, s)) return;
            json b = json::parse(req.body, nullptr, false); std::string err;
            if (b.is_discarded() || !validateProduct(b, err)) return fail(res, 400, err.empty() ? "Invalid request." : err);
            db.exec("UPDATE products SET name=?, price=?, emoji=?, description=?, active=? WHERE id=?",
                {trim(b["name"]), std::to_string(b["price"].get<long long>()), trim(b.value("emoji", "🎁")),
                 trim(b.value("description", "")), b.value("active", true) ? "1" : "0", req.matches[1]});
            if (db.changes() == 0) return fail(res, 404, "Product not found.");
            sendJson(res, 200, {{"ok", true}});
        });

        svr.Delete(R"(/api/admin/products/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
            Session s; if (!requireAdmin(req, res, s)) return;
            db.exec("UPDATE products SET active=0 WHERE id=?", {req.matches[1]});  // hide, keep order history intact
            sendJson(res, 200, {{"ok", true}});
        });

        // ---------- place order (multipart form with optional photo)
        svr.Post("/api/orders", [&](const httplib::Request& req, httplib::Response& res) {
            Session s; if (!requireLogin(req, res, s)) return;
            std::string pid = field(req, "product_id"), message = trim(field(req, "message")),
                        design = field(req, "design"), name = trim(field(req, "full_name")),
                        phone = trim(field(req, "phone")), address = trim(field(req, "address")),
                        deliv = trim(field(req, "delivery_address"));
            int qty = 0;
            try { qty = std::stoi(field(req, "quantity")); } catch (...) {}

            auto prod = db.query("SELECT * FROM products WHERE id=? AND active=1", {pid});
            if (prod.empty()) return fail(res, 400, "Selected gift is not available.");
            if (message.empty()) return fail(res, 400, "Please enter the name / message to print.");
            if (utf8Len(message) > 30) return fail(res, 400, "Message can be at most 30 characters.");
            if (!kDesigns.count(design)) return fail(res, 400, "Please select a design.");
            if (qty < 1 || qty > 20) return fail(res, 400, "Quantity must be between 1 and 20.");
            if (name.empty() || utf8Len(name) > 60) return fail(res, 400, "Please enter your full name.");
            int digits = 0; bool badPhone = false;
            for (char c : phone) { if (isdigit((unsigned char)c)) ++digits; else if (!strchr(" +-", c)) badPhone = true; }
            if (badPhone || digits < 9 || digits > 15) return fail(res, 400, "Please enter a valid phone number.");
            if (address.empty() || utf8Len(address) > 200) return fail(res, 400, "Please enter your address.");
            if (utf8Len(deliv) > 200) return fail(res, 400, "Delivery address is too long.");

            std::string photoName;
            if (req.has_file("photo") && !req.get_file_value("photo").content.empty()) {
                auto f = req.get_file_value("photo");
                std::map<std::string, std::string> ext = {{"image/jpeg", ".jpg"}, {"image/png", ".png"},
                                                          {"image/gif", ".gif"}, {"image/webp", ".webp"}};
                if (!ext.count(f.content_type)) return fail(res, 400, "Photo must be a JPG, PNG, GIF or WEBP image.");
                if (f.content.size() > 2 * 1024 * 1024) return fail(res, 400, "Photo must be smaller than 2 MB.");
                photoName = randomHex(12) + ext[f.content_type];
                std::ofstream(dataDir / "uploads" / photoName, std::ios::binary) << f.content;
            }

            long long unit = prod[0]["price"], total = unit * qty;
            long long id = db.exec(
                "INSERT INTO orders(user_id,product_id,product_name,unit_price,quantity,total,message,design,photo,"
                "full_name,phone,address,delivery_address) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)",
                {std::to_string(s.id), pid, prod[0]["name"], std::to_string(unit), std::to_string(qty),
                 std::to_string(total), message, design, photoName, name, phone, address, deliv});
            sendJson(res, 201, {{"id", id}, {"total", total}});
        });

        svr.Get("/api/orders/mine", [&](const httplib::Request& req, httplib::Response& res) {
            Session s; if (!requireLogin(req, res, s)) return;
            sendJson(res, 200, db.query("SELECT * FROM orders WHERE user_id=? ORDER BY id DESC", {std::to_string(s.id)}));
        });

        // ---------- admin: orders, stats, report
        svr.Get("/api/admin/orders", [&](const httplib::Request& req, httplib::Response& res) {
            Session s; if (!requireAdmin(req, res, s)) return;
            sendJson(res, 200, db.query("SELECT * FROM orders ORDER BY id DESC"));
        });

        svr.Put(R"(/api/admin/orders/(\d+)/status)", [&](const httplib::Request& req, httplib::Response& res) {
            Session s; if (!requireAdmin(req, res, s)) return;
            json b = json::parse(req.body, nullptr, false);
            std::string st = b.is_discarded() ? "" : b.value("status", "");
            if (!kStatuses.count(st)) return fail(res, 400, "Invalid status.");
            db.exec("UPDATE orders SET status=? WHERE id=?", {st, req.matches[1]});
            if (db.changes() == 0) return fail(res, 404, "Order not found.");
            sendJson(res, 200, {{"ok", true}});
        });

        svr.Get("/api/admin/stats", [&](const httplib::Request& req, httplib::Response& res) {
            Session s; if (!requireAdmin(req, res, s)) return;
            std::string per = periodFilter(req.get_param_value("period"));
            json out;
            out["totalOrders"] = db.query("SELECT COUNT(*) c FROM orders")[0]["c"];
            out["customers"] = db.query("SELECT COUNT(*) c FROM users WHERE role='customer'")[0]["c"];
            out["totalSales"] = db.query("SELECT COALESCE(SUM(total),0) t FROM orders WHERE status!='Cancelled'" + per)[0]["t"];
            out["periodOrders"] = db.query("SELECT COUNT(*) c FROM orders WHERE status!='Cancelled'" + per)[0]["c"];
            out["popular"] = db.query("SELECT p.name, p.emoji, SUM(o.quantity) sold FROM orders o JOIN products p ON p.id=o.product_id"
                                      " WHERE o.status!='Cancelled' GROUP BY p.id ORDER BY sold DESC LIMIT 4");
            out["byProduct"] = db.query("SELECT product_name name, SUM(quantity) sold, SUM(total) revenue FROM orders"
                                        " WHERE status!='Cancelled'" + per + " GROUP BY product_id ORDER BY revenue DESC");
            out["byStatus"] = db.query("SELECT status, COUNT(*) c FROM orders GROUP BY status");
            sendJson(res, 200, out);
        });

        std::cout << "Gift shop running at http://localhost:8080  (press Ctrl+C to stop)\n";
        if (!svr.listen("127.0.0.1", 8080)) { std::cerr << "Could not start server on port 8080 (already in use?)\n"; return 1; }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
