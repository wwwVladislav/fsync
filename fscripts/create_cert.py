#!/usr/bin/python
import sys, os, getopt

def usage():
    print('Usage:\n\tcreate_cert.py [--root] [--node]')

def main(argv):
    try:
        opts, args = getopt.getopt(argv, "hrn", ["help", "root", "node"])
    except getopt.GetoptError as err:
        print(str(err))
        usage()
        sys.exit(2)

    if len(opts) < 1:
        usage()
        sys.exit(2)

    for o, a in opts:
        if o in ("-h", "--help"):
            usage()
            sys.exit()
        elif o in ("-r", "--root"):
            p = input("Please enter a password :")
            os.system("openssl genrsa -des3 -out root.key -passout pass:{0} 2048".format(p))
            os.system("openssl req -x509 -new -key root.key -config openssl.cnf -out root.pem -days 10000 -passin pass:{0}".format(p))
        elif o in ("-n", "--node"):
            p = input("Please enter a password :")
            os.system("openssl genrsa -des3 -out node.key -passout pass:{0} 2048".format(p))
            os.system("openssl req -new -key node.key -config openssl.cnf -out node.req -passin pass:{0}".format(p))
            os.system("openssl x509 -req -in node.req -CA root.pem -CAkey root.key -CAcreateserial -out node.pem -days 10000 -sha256 -passin pass:{0}".format(p))

if __name__ == "__main__":
    main(sys.argv[1:])
