#include <stddef.h>     /* size_t, must come before libesmtp.h */
#include <libesmtp.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>

/* ------------------------------------------------------------------ */
/* Message source callback                                            */
/* ------------------------------------------------------------------ */
static const char *message_source(void **buf, int *len, void *arg)
{
    static int sent = 0;
    static const char data[] =
        "Subject: Test libESMTP\r\n"
        "\r\n"
        "Hello, this is a test email from libesmtp container!\r\n";

    /* Probe call (len == NULL): don't touch state, don't consume. */
    if (!buf || !len) {
        fprintf(stderr, "[CB] probe call (len=NULL) -> return NULL\n");
        return NULL;
    }

    if (sent) {
        fprintf(stderr, "[CB] end-of-message\n");
        *buf = NULL;
        *len = 0;
        return NULL;
    }

    sent = 1;
    *buf = (void *)data;
    *len = (int)(sizeof(data) - 1);
    fprintf(stderr, "[CB] providing %d bytes\n", *len);
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
        printf("[EVENT] connected\n");
        break;
    case SMTP_EV_MAILSTATUS: {
        const char *mailbox     = va_arg(ap, const char *);
        smtp_message_t message  = va_arg(ap, smtp_message_t);
        
        /* smtp_reverse_path_status: 取得伺服器對 MAIL FROM (寄件者反向路徑) 指令的回應狀態結構 */
        const smtp_status_t *st = smtp_reverse_path_status(message);
        printf("[EVENT] MAIL FROM <%s> -> %d %s\n",
                mailbox ? mailbox : "", st ? st->code : 0, st ? st->text : "");
        break;
    }
    case SMTP_EV_RCPTSTATUS: {
        const char *mailbox     = va_arg(ap, const char *);
        smtp_recipient_t rcpt   = va_arg(ap, smtp_recipient_t);
        
        /* smtp_recipient_status: 取得伺服器對特定 RCPT TO (收件者) 指令的回應狀態結構 */
        const smtp_status_t *st = smtp_recipient_status(rcpt);
        printf("[EVENT] RCPT TO <%s> -> %d %s\n",
                mailbox ? mailbox : "", st ? st->code : 0, st ? st->text : "");
        break;
    }
    case SMTP_EV_MESSAGEDATA: {
        smtp_message_t message = va_arg(ap, smtp_message_t);
        int size               = va_arg(ap, int);
        (void)message;
        printf("[EVENT] message data chunk (%d bytes)\n", size);
        break;
    }
    case SMTP_EV_MESSAGESENT: {
        smtp_message_t message  = va_arg(ap, smtp_message_t);
        
        /* smtp_message_transfer_status: 取得郵件內容傳輸完成後，伺服器回傳的最終傳輸狀態 */
        const smtp_status_t *st = smtp_message_transfer_status(message);
        printf("[EVENT] message sent -> %d %s\n",
                st ? st->code : 0, st ? st->text : "");
        break;
    }
    case SMTP_EV_DISCONNECT:
        printf("[EVENT] disconnected\n");
        break;
    case SMTP_EV_SYNTAXWARNING: {
        const char *resp = va_arg(ap, const char *);
        printf("[EVENT] syntax warning: %s\n", resp ? resp : "");
        break;
    }
    case SMTP_EV_ETRNSTATUS: {
        const char *domain      = va_arg(ap, const char *);
        smtp_message_t message  = va_arg(ap, smtp_message_t);
        const smtp_status_t *st = smtp_message_transfer_status(message);
        printf("[EVENT] ETRN <%s> -> %d %s\n",
                domain ? domain : "", st ? st->code : 0, st ? st->text : "");
        break;
    }
    case SMTP_EV_EXTNA_DSN:         printf("[EVENT] no DSN\n");        break;
    case SMTP_EV_EXTNA_8BITMIME:   printf("[EVENT] no 8BITMIME\n");    break;
    case SMTP_EV_EXTNA_STARTTLS:   printf("[EVENT] no STARTTLS\n");    break;
    case SMTP_EV_EXTNA_ETRN:       printf("[EVENT] no ETRN\n");        break;
    case SMTP_EV_EXTNA_CHUNKING:   printf("[EVENT] no CHUNKING\n");    break;
    case SMTP_EV_EXTNA_BINARYMIME: printf("[EVENT] no BINARYMIME\n");  break;
    case SMTP_EV_DELIVERBY_EXPIRED:
        (void)va_arg(ap, smtp_message_t);
        printf("[EVENT] DELIVERBY expired\n");
        break;
    case SMTP_EV_WEAK_CIPHER: {
        int bits         = va_arg(ap, int);
        const char *name = va_arg(ap, const char *);
        printf("[EVENT] weak cipher %s (%d bits)\n",
                name ? name : "?", bits);
        break;
    }
    case SMTP_EV_STARTTLS_OK:                          printf("[EVENT] STARTTLS OK\n");        break;
    case SMTP_EV_INVALID_PEER_CERTIFICATE:           printf("[EVENT] invalid peer cert\n");   break;
    case SMTP_EV_NO_PEER_CERTIFICATE:                  printf("[EVENT] no peer cert\n");        break;
    case SMTP_EV_WRONG_PEER_CERTIFICATE:             printf("[EVENT] wrong peer cert\n");    break;
    case SMTP_EV_NO_CLIENT_CERTIFICATE:              printf("[EVENT] no client cert\n");      break;
    case SMTP_EV_UNUSABLE_CLIENT_CERTIFICATE:        printf("[EVENT] unusable client cert\n");break;
    case SMTP_EV_UNUSABLE_CA_LIST:                     printf("[EVENT] unusable CA list\n");    break;
    default:
        printf("[EVENT] unhandled id = %d\n", event_no);
        break;
    }

    va_end(ap);
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    smtp_session_t session;
    smtp_message_t message;
    const char *server = "smtp4dev:25";

    signal(SIGPIPE, SIG_IGN);
    printf("[MAIN] program started\n");

    /* smtp_create_session: 建立並初始化一個新的 SMTP 工作階段 (session) 物件 */
    session = smtp_create_session();
    if (!session) {
        fprintf(stderr, "[ERROR] create_session failed\n");
        return 1;
    }

    /* smtp_set_eventcb: 註冊事件回呼函式，用以接收連線、驗證狀態、憑證或伺服器回應等事件通知 */
    if (!smtp_set_eventcb(session, event_cb, NULL)) {
        fprintf(stderr, "[ERROR] set_eventcb failed\n");
        smtp_destroy_session(session);
        return 1;
    }

    /* smtp_set_server: 設定要連線的 SMTP 伺服器位址（可包含主機名稱與通訊埠，例如 "smtp4dev:25"） */
    smtp_set_server(session, server);

    /* smtp_add_message: 在此 SMTP session 中新增一封郵件物件（libESMTP 支援在一個 session 內發送多封郵件） */
    message = smtp_add_message(session);
    if (!message) {
        fprintf(stderr, "[ERROR] add_message failed\n");
        smtp_destroy_session(session);
        return 1;
    }

    /* smtp_set_reverse_path: 設定郵件的寄件者信箱（MAIL FROM 指令對應的地址） */
    smtp_set_reverse_path(message, "sender@example.com");

    /* smtp_add_recipient: 新增郵件的收件者信箱（RCPT TO 指令對應的地址，可呼叫多次以加入多個收件者） */
    smtp_add_recipient(message, "recipient@example.com");

    /* smtp_set_messagecb: 設定郵件內容來源的回呼函式，用於提供信件的標頭與內文數據區塊 */
    smtp_set_messagecb(message, message_source, NULL);

    printf("[MAIN] starting SMTP session...\n");
    
    /* smtp_start_session: 啟動 SMTP 傳輸過程，開始連線伺服器並執行完整的 SMTP 協議交握與發信流程 */
    if (!smtp_start_session(session)) {
        char errbuf[256];
        
        /* smtp_errno: 取得最近一次 libESMTP 操作失敗時所產生的錯誤代碼 */
        // smtp_strerror: 將 libESMTP 的錯誤代碼轉換成人類可讀的錯誤訊息字串
        smtp_strerror(smtp_errno(), errbuf, sizeof(errbuf));
        fprintf(stderr, "[ERROR] start_session failed: %s\n", errbuf);
        
        /* smtp_destroy_session: 銷毀 SMTP session 並釋放其佔用的所有記憶體與網路資源 */
        smtp_destroy_session(session);
        return 1;
    }

    printf("[MAIN] success\n");
    
    /* smtp_destroy_session: 銷毀 SMTP session 並釋放其佔用的所有記憶體與網路資源 */
    smtp_destroy_session(session);
    printf("[MAIN] program finished\n");
    return 0;
}
