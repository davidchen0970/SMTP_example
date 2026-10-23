#include <stddef.h>
#include <libesmtp.h>
#include <csignal>
#include <iostream>
#include <string>
#include <memory>
#include <cstdarg>

// ------------------------------------------------------------------
// RAII 包裝器：自動管理 smtp_session_t 的生命週期
// ------------------------------------------------------------------
class SmtpSession {
private:
    smtp_session_t session_;

public:
    SmtpSession() : session_(smtp_create_session()) {}
    
    ~SmtpSession() {
        if (session_) {
            smtp_destroy_session(session_);
        }
    }

    // 禁用複製（防止雙重釋放），但允許移動
    SmtpSession(const SmtpSession&) = delete;
    SmtpSession& operator=(const SmtpSession&) = delete;

    // 取得底層 C 指標
    smtp_session_t get() const { return session_; }
    
    // 支援 bool 判斷是否建立成功
    explicit operator bool() const { return session_ != nullptr; }
};

// libESMTP 要求的 C 語言回呼函式必須宣告在 extern "C" 內
extern "C" {

/* ------------------------------------------------------------------ */
/* Message source callback                                            */
/* ------------------------------------------------------------------ */
static const char *message_source(void **buf, int *len, void *arg)
{
    static int sent = 0;
    static constexpr char data[] =
        "Subject: Test libESMTP over STARTTLS (Modern C++)\r\n"
        "\r\n"
        "Hello from libESMTP + STARTTLS (Modern C++)!\r\n";

    if (!buf || !len)
        return nullptr;

    if (sent) {
        *buf = nullptr;
        *len = 0;
        return nullptr;
    }

    sent = 1;
    *buf = const_cast<char *>(data);
    *len = static_cast<int>(sizeof(data) - 1);
    return data;
}

/* ------------------------------------------------------------------ */
/* Event callback                                                     */
/* SSL * is an opaque type here, so we use void * to consume it.      */
/* ------------------------------------------------------------------ */
static void event_cb(smtp_session_t session, int event_no, void *arg, ...)
{
    va_list ap;
    va_start(ap, arg);

    switch (event_no) {

    /* ---- Core ---- */
    case SMTP_EV_CONNECT:
        std::cout << "[EVENT] connected\n";
        break;

    case SMTP_EV_MAILSTATUS: {
        const char *mailbox     = va_arg(ap, const char *);
        smtp_message_t message  = va_arg(ap, smtp_message_t);
        const smtp_status_t *st = smtp_reverse_path_status(message);
        std::cout << "[EVENT] MAIL FROM <" << (mailbox ? mailbox : "") 
                  << "> -> " << (st ? st->code : 0) << " " << (st ? st->text : "") << "\n";
        break;
    }

    case SMTP_EV_RCPTSTATUS: {
        const char *mailbox     = va_arg(ap, const char *);
        smtp_recipient_t rcpt   = va_arg(ap, smtp_recipient_t);
        const smtp_status_t *st = smtp_recipient_status(rcpt);
        std::cout << "[EVENT] RCPT TO <" << (mailbox ? mailbox : "") 
                  << "> -> " << (st ? st->code : 0) << " " << (st ? st->text : "") << "\n";
        break;
    }

    case SMTP_EV_MESSAGESENT: {
        smtp_message_t message  = va_arg(ap, smtp_message_t);
        const smtp_status_t *st = smtp_message_transfer_status(message);
        std::cout << "[EVENT] message sent -> " << (st ? st->code : 0) << " " << (st ? st->text : "") << "\n";
        break;
    }

    case SMTP_EV_DISCONNECT:
        std::cout << "[EVENT] disconnected\n";
        break;

    /* ---- TLS ---- */
    case SMTP_EV_STARTTLS_OK: {
        void       *ssl    = va_arg(ap, void *);
        const char *cipher = va_arg(ap, const char *);
        int         bits   = va_arg(ap, int);
        (void)ssl;
        std::cout << "[TLS] STARTTLS negotiated: cipher=" << (cipher ? cipher : "?") 
                  << " bits=" << bits << "\n";
        break;
    }

    case SMTP_EV_INVALID_PEER_CERTIFICATE: {
        long  verify = va_arg(ap, long);
        int  *ok     = va_arg(ap, int *);
        void *ssl    = va_arg(ap, void *);
        (void)ssl;
        std::cout << "[TLS] peer certificate INVALID (verify=" << verify << ") -> accepting anyway\n";
        if (ok) *ok = 1;
        break;
    }

    case SMTP_EV_NO_PEER_CERTIFICATE: {
        int *ok = va_arg(ap, int *);
        std::cout << "[TLS] no peer certificate -> accepting anyway\n";
        if (ok) *ok = 1;
        break;
    }

    case SMTP_EV_WRONG_PEER_CERTIFICATE: {
        int        *ok     = va_arg(ap, int *);
        const char *domain = va_arg(ap, const char *);
        void       *ssl    = va_arg(ap, void *);
        (void)ssl;
        std::cout << "[TLS] certificate domain mismatch (cert=" << (domain ? domain : "?") 
                  << ") -> accepting anyway\n";
        if (ok) *ok = 1;
        break;
    }

    case SMTP_EV_EXTNA_STARTTLS:
        std::cout << "[TLS] server does NOT support STARTTLS\n";
        break;

    default:
        std::cout << "[EVENT] id = " << event_no << "\n";
        break;
    }

    va_end(ap);
}

} // extern "C"

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    std::signal(SIGPIPE, SIG_IGN);
    std::cout << "[MAIN] program started (Modern C++)\n";

    // 1. 使用 RAII 自動管理 session 生命週期
    SmtpSession session_wrapper;
    if (!session_wrapper) {
        std::cerr << "[ERROR] create_session failed\n";
        return 1;
    }
    smtp_session_t session = session_wrapper.get();

    // 2. 設定事件回呼
    if (!smtp_set_eventcb(session, event_cb, nullptr)) {
        std::cerr << "[ERROR] set_eventcb failed\n";
        return 1;
    }

    std::string server = "smtp4dev:25";
    smtp_set_server(session, server.c_str());

    // 3. 啟用 STARTTLS 支援
    if (!smtp_starttls_enable(session, Starttls_REQUIRED)) {
        std::cerr << "[ERROR] smtp_starttls_enable failed (libESMTP built without OpenSSL?)\n";
        return 1;
    }
    std::cout << "[MAIN] STARTTLS required\n";

    // 4. 新增信件與設定路徑
    smtp_message_t message = smtp_add_message(session);
    if (!message) {
        std::cerr << "[ERROR] add_message failed\n";
        return 1;
    }

    smtp_set_reverse_path(message, "sender@example.com");
    smtp_add_recipient(message, "recipient@example.com");
    smtp_set_messagecb(message, message_source, nullptr);

    std::cout << "[MAIN] starting SMTP session...\n";
    if (!smtp_start_session(session)) {
        char errbuf[256];
        smtp_strerror(smtp_errno(), errbuf, sizeof(errbuf));
        std::cerr << "[ERROR] start_session failed: " << errbuf << "\n";
        return 1;
    }

    std::cout << "[MAIN] success\n";
    std::cout << "[MAIN] program finished\n";
    return 0; // session_wrapper 在此自動解構銷毀
}