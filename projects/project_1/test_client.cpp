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
            /* smtp_destroy_session: 銷毀 SMTP session 並釋放其佔用的所有記憶體與網路資源 */
            smtp_destroy_session(session_);
        }
    }

    // 禁用複製（防止雙重釋放），但允許移動（若有需要）
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
        "Subject: Test libESMTP (Modern C++)\r\n"
        "\r\n"
        "Hello, this is a test email from a modern C++ application!\r\n";

    /* Probe call (len == nullptr): don't touch state, don't consume. */
    if (!buf || !len) {
        std::cerr << "[CB] probe call (len=nullptr) -> return nullptr\n";
        return nullptr;
    }

    if (sent) {
        std::cerr << "[CB] end-of-message\n";
        *buf = nullptr;
        *len = 0;
        return nullptr;
    }

    sent = 1;
    *buf = const_cast<char *>(data);
    *len = static_cast<int>(sizeof(data) - 1);
    std::cerr << "[CB] providing " << *len << " bytes\n";
    return data;
}

/* ------------------------------------------------------------------ */
/* SMTP event callback (all events this libESMTP build supports)      */
/* ------------------------------------------------------------------ */
static void event_cb(smtp_session_t session, int event_no, void *arg, ...)
{
    va_list ap;
    va_start(ap, arg);

    switch (event_no) {
    case SMTP_EV_CONNECT:
        std::cout << "[EVENT] connected\n";
        break;
    case SMTP_EV_MAILSTATUS: {
        const char *mailbox     = va_arg(ap, const char *);
        smtp_message_t message  = va_arg(ap, smtp_message_t);
        
        /* smtp_reverse_path_status: 取得伺服器對 MAIL FROM (寄件者反向路徑) 指令的回應狀態結構 */
        const smtp_status_t *st = smtp_reverse_path_status(message);
        std::cout << "[EVENT] MAIL FROM <" << (mailbox ? mailbox : "")  
                  << "> -> " << (st ? st->code : 0) << " " << (st ? st->text : "") << "\n";
        break;
    }
    case SMTP_EV_RCPTSTATUS: {
        const char *mailbox     = va_arg(ap, const char *);
        smtp_recipient_t rcpt   = va_arg(ap, smtp_recipient_t);
        
        /* smtp_recipient_status: 取得伺服器對特定 RCPT TO (收件者) 指令的回應狀態結構 */
        const smtp_status_t *st = smtp_recipient_status(rcpt);
        std::cout << "[EVENT] RCPT TO <" << (mailbox ? mailbox : "")  
                  << "> -> " << (st ? st->code : 0) << " " << (st ? st->text : "") << "\n";
        break;
    }
    case SMTP_EV_MESSAGEDATA: {
        int size = va_arg(ap, int);
        std::cout << "[EVENT] message data chunk (" << size << " bytes)\n";
        break;
    }
    case SMTP_EV_MESSAGESENT: {
        smtp_message_t message  = va_arg(ap, smtp_message_t);
        
        /* smtp_message_transfer_status: 取得郵件內容傳輸完成後，伺服器回傳的最終傳輸狀態 */
        const smtp_status_t *st = smtp_message_transfer_status(message);
        std::cout << "[EVENT] message sent -> " << (st ? st->code : 0) << " " << (st ? st->text : "") << "\n";
        break;
    }
    case SMTP_EV_DISCONNECT:
        std::cout << "[EVENT] disconnected\n";
        break;
    default:
        std::cout << "[EVENT] unhandled id = " << event_no << "\n";
        break;
    }

    va_end(ap);
}

} // extern "C"

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */
int main()
{
    std::signal(SIGPIPE, SIG_IGN);
    std::cout << "[MAIN] program started (Modern C++)\n";

    // 1. 使用 RAII 包裝器，離開作用域時會自動銷毀 session，無需到處手動呼叫 smtp_destroy_session
    /* smtp_create_session: 建立並初始化一個新的 SMTP 工作階段 (session) 物件 */
    SmtpSession session_wrapper;
    if (!session_wrapper) {
        std::cerr << "[ERROR] create_session failed\n";
        return 1;
    }
    smtp_session_t session = session_wrapper.get();

    // 2. 設定事件回呼
    /* smtp_set_eventcb: 註冊事件回呼函式，用以接收連線、驗證狀態、憑證或伺服器回應等事件通知 */
    if (!smtp_set_eventcb(session, event_cb, nullptr)) {
        std::cerr << "[ERROR] set_eventcb failed\n";
        return 1;
    }

    // 3. 設定伺服器與信件內容
    std::string server = "smtp4dev:25";
    /* smtp_set_server: 設定要連線的 SMTP 伺服器位址（可包含主機名稱與通訊埠，例如 "smtp4dev:25"） */
    smtp_set_server(session, server.c_str());

    /* smtp_add_message: 在此 SMTP session 中新增一封郵件物件（libESMTP 支援在一個 session 內發送多封郵件） */
    smtp_message_t message = smtp_add_message(session);
    if (!message) {
        std::cerr << "[ERROR] add_message failed\n";
        return 1;
    }

    /* smtp_set_reverse_path: 設定郵件的寄件者信箱（MAIL FROM 指令對應的地址） */
    smtp_set_reverse_path(message, "sender@example.com");
    /* smtp_add_recipient: 新增郵件的收件者信箱（RCPT TO 指令對應的地址，可呼叫多次以加入多個收件者） */
    smtp_add_recipient(message, "recipient@example.com");
    /* smtp_set_messagecb: 設定郵件內容來源的回呼函式，用於提供信件的標頭與內文數據區塊 */
    smtp_set_messagecb(message, message_source, nullptr);

    std::cout << "[MAIN] starting SMTP session...\n";

    // 4. 啟動階段
    /* smtp_start_session: 啟動 SMTP 傳輸過程，開始連線伺服器並執行完整的 SMTP 協議交握與發信流程 */
    if (!smtp_start_session(session)) {
        char errbuf[256];
        
        /* smtp_errno: 取得最近一次 libESMTP 操作失敗時所產生的錯誤代碼 */
        // smtp_strerror: 將 libESMTP 的錯誤代碼轉換成人類可讀的錯誤訊息字串
        smtp_strerror(smtp_errno(), errbuf, sizeof(errbuf));
        std::cerr << "[ERROR] start_session failed: " << errbuf << "\n";
        return 1; // session_wrapper 在這裡也會自動解構釋放資源
    }

    std::cout << "[MAIN] success\n";
    std::cout << "[MAIN] program finished\n";
    return 0; // session_wrapper 離開作用域自動解構，安全釋放資源
}