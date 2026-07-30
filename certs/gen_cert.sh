#!/opt/homebrew/bin/bash

HOSTNAME=$1
CERTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
OPENSSL=/opt/miniconda3/bin/openssl

# Generate private key
$OPENSSL genrsa -out "$CERTS_DIR/$HOSTNAME.key" 2048 2>/dev/null

# Generate CSR
$OPENSSL req -new \
  -key "$CERTS_DIR/$HOSTNAME.key" \
  -out "$CERTS_DIR/$HOSTNAME.csr" \
  -subj "/CN=$HOSTNAME/O=LocalDev/C=US" 2>/dev/null

# Write SAN config to temp file
echo "subjectAltName=DNS:$HOSTNAME" > "$CERTS_DIR/$HOSTNAME.ext"

# Sign the cert
$OPENSSL x509 -req \
  -in "$CERTS_DIR/$HOSTNAME.csr" \
  -CA "$CERTS_DIR/ca.crt" \
  -CAkey "$CERTS_DIR/ca.key" \
  -CAcreateserial \
  -out "$CERTS_DIR/$HOSTNAME.crt" \
  -days 365 \
  -extfile "$CERTS_DIR/$HOSTNAME.ext" 2>/dev/null

# Clean up temp files
rm -f "$CERTS_DIR/$HOSTNAME.csr" "$CERTS_DIR/$HOSTNAME.ext"

echo "$CERTS_DIR/$HOSTNAME.crt $CERTS_DIR/$HOSTNAME.key"