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
        "Subject: Test libESMTP over STARTTLS\r\n"
        "\r\n"
        "Hello from libESMTP + STARTTLS!\r\n";

    if (!buf || !len)
        return NULL;

    if (sent) {
        *buf = NULL;
        *len = 0;
        return NULL;
    }

    sent = 1;
    *buf = (void *)data;
    *len = (int)(sizeof(data) - 1);
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
        printf("[EVENT] connected\n");
        break;

    case SMTP_EV_MAILSTATUS: {
        const char *mailbox     = va_arg(ap, const char *);
        smtp_message_t message  = va_arg(ap, smtp_message_t);
        const smtp_status_t *st = smtp_reverse_path_status(message);
        printf("[EVENT] MAIL FROM <%s> -> %d %s\n",
               mailbox ? mailbox : "", st ? st->code : 0, st ? st->text : "");
        break;
    }

    case SMTP_EV_RCPTSTATUS: {
        const char *mailbox     = va_arg(ap, const char *);
        smtp_recipient_t rcpt   = va_arg(ap, smtp_recipient_t);
        const smtp_status_t *st = smtp_recipient_status(rcpt);
        printf("[EVENT] RCPT TO <%s> -> %d %s\n",
               mailbox ? mailbox : "", st ? st->code : 0, st ? st->text : "");
        break;
    }

    case SMTP_EV_MESSAGESENT: {
        smtp_message_t message  = va_arg(ap, smtp_message_t);
        const smtp_status_t *st = smtp_message_transfer_status(message);
        printf("[EVENT] message sent -> %d %s\n",
               st ? st->code : 0, st ? st->text : "");
        break;
    }

    case SMTP_EV_DISCONNECT:
        printf("[EVENT] disconnected\n");
        break;

    /* ---- TLS ---- */
    case SMTP_EV_STARTTLS_OK: {
        void       *ssl    = va_arg(ap, void *);
        const char *cipher = va_arg(ap, const char *);
        int         bits   = va_arg(ap, int);
        (void)ssl;
        printf("[TLS] STARTTLS negotiated: cipher=%s bits=%d\n",
               cipher ? cipher : "?", bits);
        break;
    }

    case SMTP_EV_INVALID_PEER_CERTIFICATE: {
        long  verify = va_arg(ap, long);
        int  *ok     = va_arg(ap, int *);
        void *ssl    = va_arg(ap, void *);
        (void)ssl;
        printf("[TLS] peer certificate INVALID (verify=%ld) -> accepting anyway\n",
               verify);
        if (ok) *ok = 1;
        break;
    }

    case SMTP_EV_NO_PEER_CERTIFICATE: {
        int *ok = va_arg(ap, int *);
        printf("[TLS] no peer certificate -> accepting anyway\n");
        if (ok) *ok = 1;
        break;
    }

    case SMTP_EV_WRONG_PEER_CERTIFICATE: {
        int        *ok     = va_arg(ap, int *);
        const char *domain = va_arg(ap, const char *);
        void       *ssl    = va_arg(ap, void *);
        (void)ssl;
        printf("[TLS] certificate domain mismatch (cert=%s) -> accepting anyway\n",
               domain ? domain : "?");
        if (ok) *ok = 1;
        break;
    }

    case SMTP_EV_EXTNA_STARTTLS:
        printf("[TLS] server does NOT support STARTTLS\n");
        break;

    default:
        printf("[EVENT] id = %d\n", event_no);
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

    session = smtp_create_session();
    if (!session) {
        fprintf(stderr, "[ERROR] create_session failed\n");
        return 1;
    }

    if (!smtp_set_eventcb(session, event_cb, NULL)) {
        fprintf(stderr, "[ERROR] set_eventcb failed\n");
        smtp_destroy_session(session);
        return 1;
    }

    smtp_set_server(session, server);

    if (!smtp_starttls_enable(session, Starttls_REQUIRED)) {
        fprintf(stderr, "[ERROR] smtp_starttls_enable failed "
                        "(libESMTP built without OpenSSL?)\n");
        smtp_destroy_session(session);
        return 1;
    }
    printf("[MAIN] STARTTLS required\n");

    message = smtp_add_message(session);
    if (!message) {
        fprintf(stderr, "[ERROR] add_message failed\n");
        smtp_destroy_session(session);
        return 1;
    }

    smtp_set_reverse_path(message, "sender@example.com");
    smtp_add_recipient(message, "recipient@example.com");
    smtp_set_messagecb(message, message_source, NULL);

    printf("[MAIN] starting SMTP session...\n");
    if (!smtp_start_session(session)) {
        char errbuf[256];
        smtp_strerror(smtp_errno(), errbuf, sizeof(errbuf));
        fprintf(stderr, "[ERROR] start_session failed: %s\n", errbuf);
        smtp_destroy_session(session);
        return 1;
    }

    printf("[MAIN] success\n");
    smtp_destroy_session(session);
    return 0;
}