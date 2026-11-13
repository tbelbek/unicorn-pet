#pragma once
// The public half of the release signing key, written by tools/release.py (the private half stays at
// ~/.config/unicorn-pet/release-key.pem, outside the repo). Only images it signed are installed.
static const char RELEASE_PUBKEY[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEVzM6eWuhUIXtaJoEc0vsgh2Dgtt0\n"
    "cR7ikZQ3ZlmpBnUu3X2nw+GXH32itaQ5YkQC+Yb0y9Q6DmQHAwEveqgD4A==\n"
    "-----END PUBLIC KEY-----\n";
