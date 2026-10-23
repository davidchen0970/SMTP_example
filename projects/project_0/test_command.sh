openssl s_client -starttls smtp -connect smtp4dev:25 -CAfile /certs/smtp4dev.crt

# 進入後輸入
# EHLO client.example.com

# 要離開打
# QUIT