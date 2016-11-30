#!/usr/bin/python
import sys, os

def main(argv):
    p = input("Please enter a password :")
    os.system("openssl genrsa -des3 -out rsa_key.pem -passout pass:{0} 2048".format(p))
    os.system("openssl req -x509 -new -key rsa_key.pem -config openssl.cnf -out selfcert.crt -days 10000 -passin pass:{0}".format(p))

if __name__ == "__main__":
    main(sys.argv[1:])
