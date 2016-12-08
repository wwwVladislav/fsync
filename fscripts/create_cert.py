#!/usr/bin/python
import sys, os, getopt

def usage():
    print('Usage:\n\tcreate_cert.py [--root] [--intermediate] [--node]')

def main(argv):
    try:
        opts, args = getopt.getopt(argv, "hrin", ["help", "root", "intermediate", "node"])
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
            os.chdir("root/ca")
            os.system("openssl genrsa -aes256 -out private/ca.key.pem -passout pass:{0} 4096".format(p))
            os.system("openssl req -config openssl.cnf -key private/ca.key.pem -new -x509 -days 7300 -passin pass:{0} -sha256 -extensions v3_ca -out certs/ca.cert.pem".format(p))
        elif o in ("-i", "--intermediate"):
            p = input("Please enter a password :")
            os.chdir("root/ca")
            os.system("openssl genrsa -aes256 -out intermediate/private/intermediate.key.pem -passout pass:{0} 4096".format(p))
            os.system("openssl req -config intermediate/openssl.cnf -new -sha256 -passin pass:{0} -key intermediate/private/intermediate.key.pem -out intermediate/csr/intermediate.csr.pem".format(p))
            os.system("openssl ca -config openssl.cnf -extensions v3_intermediate_ca -days 3650 -notext -md sha256 -passin pass:{0} -in intermediate/csr/intermediate.csr.pem -out intermediate/certs/intermediate.cert.pem".format(p))

            with open('intermediate/certs/intermediate.cert.pem', 'r') as fi:
                icer = fi.read()
                with open('certs/ca.cert.pem', 'r') as fr:
                    rcer = fr.read()
                    with open('intermediate/certs/ca-chain.cert.pem', 'w') as f:
                        f.write(icer)
                        f.write(rcer)
        elif o in ("-n", "--node"):
            p = input("Please enter a password :")
            os.chdir("root/ca")
            os.system("openssl genrsa -aes256 -out intermediate/private/node.key.pem -passout pass:{0} 2048".format(p))
            os.system("openssl req -config intermediate/openssl.cnf -key intermediate/private/node.key.pem -new -sha256 -out intermediate/csr/node.csr.pem -passin pass:{0}".format(p))
            os.system("openssl ca -config intermediate/openssl.cnf -extensions server_cert -days 375 -notext -md sha256 -passin pass:{0} -in intermediate/csr/node.csr.pem -out intermediate/certs/node.cert.pem".format(p))

if __name__ == "__main__":
    main(sys.argv[1:])
