import crypto

# in its own file so we can track where it is imported more effectively
# (should never be imported by clients)
CROSS_DIR_KEY = crypto.aes_expand_key(
    b'\xa1\x11\x1c\xf0\xf6+\xba\x02%\xd2f\xe7\xa6\x94\x86\xfe')
