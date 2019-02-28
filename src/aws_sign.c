#include "mql_classes.h"
#include "foreign/sha256.h"
#include "foreign/hmac_sha256.h"

#include <string.h>
#include <time.h>

#define DATE_LEN 9
#define MAX_URI 2000
#define SHA256_DIGEST_HEX_SIZE (SHA256_DIGEST_SIZE * 2 + 1)
#define MAX_CANONICAL_REQUEST_LEN 5000
#define MAX_STRING_TO_SIGN_LEN 1000

struct _aws_sign_t {
    char *access_key;
    char *secret;
    char *region;
    char *service_name;
    BYTE *cached_key[SHA256_DIGEST_SIZE];
    char cached_date[DATE_LEN];
};

aws_sign_t *aws_sign_new (
        const char *access_key,
        const char *secret,
        const char *region,
        const char *service_name) {
    aws_sign_t *self = (aws_sign_t *) zmalloc (sizeof (aws_sign_t));
    assert (self);
    self->access_key = strdup (access_key);
    self->secret = strdup (secret);
    self->region = strdup (region);
    self->service_name = strdup (service_name);

    return self;
}

void aws_sign_destroy (aws_sign_t **self_p) {
    assert (self_p);
    aws_sign_t *self = *self_p;

    if (self) {
        memset(self->secret, 0, strlen(self->secret));

        zstr_free (&self->access_key);
        zstr_free (&self->secret);
        zstr_free (&self->region);
        zstr_free (&self->service_name);

        free (self);
        *self_p = NULL;
    }
}

static void get_date (char *output, const char *datetime) {
    strncpy (output, datetime, DATE_LEN - 1);
    output[DATE_LEN - 1] = '\0';
}

static void to_hex (char *output, byte *data, size_t size) {
    static const char hex_char[] = "0123456789abcdef";

    uint byte_nbr;
    for (byte_nbr = 0; byte_nbr < size; byte_nbr++) {
        output[byte_nbr * 2 + 0] = hex_char[data[byte_nbr] >> 4];
        output[byte_nbr * 2 + 1] = hex_char[data[byte_nbr] & 15];
    }
    output[size * 2] = 0;
}

static void compute_hash (char *output, const byte *request_payload, size_t request_payload_size) {
    SHA256_CTX ctx;
    byte hash[SHA256_DIGEST_SIZE];

    sha256_init (&ctx);
    sha256_update (&ctx, request_payload, request_payload_size);
    sha256_final (&ctx, hash);

    to_hex (output, hash, SHA256_DIGEST_SIZE);
}

bool should_encode_char (char c, bool legacy) {

    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        return false;
    else {
        switch (c) {
            // §2.3 unreserved characters
            case '-':
            case '_':
            case '.':
            case '~':
            case '/':
                return false;
            case '$':
            case '&':
            case ',':
            case ':':
            case ';':
            case '=':
            case '@':
                // The path section of the URL allow reserved characters to appear unescaped
                // RFC 3986 §2.2 Reserved characters
                // NOTE: this implementation does not accurately implement the RFC on purpose to accommodate for
                // discrepancies in the implementations of URL encoding between AWS services for legacy reasons.
                return !legacy;

            default:
                return true;
        }
    }
}

static void encode_path (char *output, const char *path, bool legacy) {
    if (strlen (path) == 0) {
        strcpy (output, path);
        return;
    }

    size_t index = 0;

    // TODO: we assume path cannot be larger than MAX_URI
    for (const char *c = path; *c != '\0'; c++) {
        if (should_encode_char (*c, legacy)) {
            sprintf (output + index, "%%%02X", *c);
            index += 3;
        } else {
            output[index] = *c;
            index++;
        }
    }

    output[index] = '\0';
}

static void encode_path_double (char *output, const char *path) {
    char once[MAX_URI];
    encode_path (once, path, true);
    encode_path (output, once, false);
}

static void create_canonical_request (
        aws_sign_t *self,
        char *output,
        const char *method,
        const char *host,
        const char *path,
        const char *query,
        const char *datetime,
        const byte *request_payload,
        size_t request_payload_size) {

    (void) self;

    char escaped_path[MAX_URI];
    encode_path_double (escaped_path, path);

    char request_payload_hash[SHA256_DIGEST_HEX_SIZE];
    compute_hash (request_payload_hash, request_payload, request_payload_size);

    // TODO: we assume query is already canonical, we need to canonicalize query as well

    sprintf (output, "%s\n%s\n%s\nhost:%s\nx-amz-date:%s\n\nhost;x-amz-date\n%s", method, escaped_path, query,
             host, datetime, request_payload_hash);
}

static void create_canonical_request_hash (
        aws_sign_t *self,
        char *output,
        const char *method,
        const char *host,
        const char *path,
        const char *query,
        const char *datetime,
        const byte *request_payload,
        size_t request_payload_size) {

    char request[MAX_CANONICAL_REQUEST_LEN];
    create_canonical_request (
            self,
            request,
            method,
            host,
            path,
            query,
            datetime,
            request_payload,
            request_payload_size);

    compute_hash (output, (const byte *) request, strlen (request));
}

static void create_string_to_sign (
        aws_sign_t *self,
        char *output,
        const char *canonical_request_hash,
        const char *datetime,
        const char *date) {
    sprintf (output, "AWS4-HMAC-SHA256\n%s\n%s/%s/%s/aws4_request\n%s",
             datetime, date, self->region, self->service_name, canonical_request_hash);
}

static void compute_hmac (const byte *key, size_t key_size, const char *data, byte *hmac) {
    hmac_sha256 (key, key_size, (const BYTE *) data, strlen (data), hmac);
}

static void calculate_signature (
        aws_sign_t *self, char *signature, const char *date, const char *string) {
    byte hmac[SHA256_DIGEST_SIZE];

    if (strcmp(self->cached_date, date) == 0)
        memcpy (hmac, self->cached_key, SHA256_DIGEST_SIZE);
    else {
        size_t size = strlen (self->secret) + 4;

        assert (size < 255);
        char ksecret[256];
        strcpy (ksecret, "AWS4");
        strcat (ksecret, self->secret);

        compute_hmac ((const byte *) ksecret, size, date, hmac);
        compute_hmac (hmac, SHA256_DIGEST_SIZE, self->region, hmac);
        compute_hmac (hmac, SHA256_DIGEST_SIZE, self->service_name, hmac);
        compute_hmac (hmac, SHA256_DIGEST_SIZE, "aws4_request", hmac);

        memcpy (self->cached_key, hmac, SHA256_DIGEST_SIZE);
        strcpy (self->cached_date, date);

        // Cleanup the secret
        memset(ksecret, 0, 256);
    }

    compute_hmac (hmac, SHA256_DIGEST_SIZE, string, hmac);

    to_hex (signature, hmac, SHA256_DIGEST_SIZE);
}

static void create_authorization_header (aws_sign_t *self, char *output, const char *date, const char *signature) {
    sprintf (
            output,
            "AWS4-HMAC-SHA256 Credential=%s/%s/%s/%s/aws4_request, SignedHeaders=host;x-amz-date, Signature=%s",
            self->access_key, date, self->region, self->service_name, signature);
}

void aws_sign (
        aws_sign_t *self,
        char *output,
        const char *method,
        const char *host,
        const char *path,
        const char *query,
        const char *datetime,
        const char *request_payload) {

    char date[DATE_LEN];
    get_date (date, datetime);

    size_t request_payload_size = request_payload ? strlen (request_payload) : 0;

    char canonical_request_hash[SHA256_DIGEST_HEX_SIZE];
    create_canonical_request_hash (self, canonical_request_hash, method, host, path, query, datetime, request_payload,
                                   request_payload_size);

    char string_to_sign[MAX_STRING_TO_SIGN_LEN];
    create_string_to_sign (self, string_to_sign, canonical_request_hash, datetime, date);

    char signature[SHA256_DIGEST_HEX_SIZE];
    calculate_signature (self, signature, date, string_to_sign);

    create_authorization_header (self, output, date, signature);
}

void aws_sign_test () {
    printf (" * aws_sign: ");


    const char *datetime = "20150830T123600Z";
    char date[DATE_LEN];
    get_date (date, datetime);
    aws_sign_t *self = aws_sign_new ("AKIDEXAMPLE", "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY", "us-east-1", "service");

    char canonical_request[MAX_CANONICAL_REQUEST_LEN];
    create_canonical_request (
            self,
            canonical_request,
            "GET",
            "example.amazonaws.com",
            "/",
            "Param1=value1&Param2=value2",
            "20150830T123600Z",
            NULL, 0
    );
    assert(streq (canonical_request, "GET\n"
                                     "/\n"
                                     "Param1=value1&Param2=value2\n"
                                     "host:example.amazonaws.com\n"
                                     "x-amz-date:20150830T123600Z\n"
                                     "\n"
                                     "host;x-amz-date\n"
                                     "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    char canonical_request_hash[SHA256_DIGEST_HEX_SIZE];
    create_canonical_request_hash (
            self,
            canonical_request_hash,
            "GET",
            "example.amazonaws.com",
            "/",
            "Param1=value1&Param2=value2",
            "20150830T123600Z",
            NULL, 0
    );
    assert(streq (canonical_request_hash, "816cd5b414d056048ba4f7c5386d6e0533120fb1fcfa93762cf0fc39e2cf19e0"));

    char string_to_sign[MAX_STRING_TO_SIGN_LEN];
    create_string_to_sign (self, string_to_sign, canonical_request_hash, datetime, date);
    assert(streq (string_to_sign, "AWS4-HMAC-SHA256\n"
                                  "20150830T123600Z\n"
                                  "20150830/us-east-1/service/aws4_request\n"
                                  "816cd5b414d056048ba4f7c5386d6e0533120fb1fcfa93762cf0fc39e2cf19e0"));

    char signature[SHA256_DIGEST_HEX_SIZE];
    calculate_signature (self, signature, date, string_to_sign);
    assert(streq (signature, "b97d918cfa904a5beff61c982a1b6f458b799221646efd99d3219ec94cdf2500"));

    char header[MAX_AUTHORIZATION_LEN];
    aws_sign (
            self,
            header,
            "GET",
            "example.amazonaws.com",
            "/",
            "Param1=value1&Param2=value2",
            "20150830T123600Z",
            NULL);

    assert(streq (header,
                  "AWS4-HMAC-SHA256 Credential=AKIDEXAMPLE/20150830/us-east-1/service/aws4_request, SignedHeaders=host;x-amz-date, Signature=b97d918cfa904a5beff61c982a1b6f458b799221646efd99d3219ec94cdf2500"));

    aws_sign_destroy (&self);

    printf ("OK\n");
}