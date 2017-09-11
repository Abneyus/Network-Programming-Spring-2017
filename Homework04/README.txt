Elijah Abney
Jacob Farnsworth

Tested on Linux Subsystem for Windows

None of the errors I know about are reproducable. Occasionally it will fail on, say, Assignment4.pdf but then I run it again and it works fine.

Application Layer:

Query
        Client                                  Server
        "query (filename) (mtime)"
                                                "put" or "get"


Contents
        Client                                  Server
        "contents"
                                                (size of .4220_file_list.txt)
        "ACK"
                                                (contents of .4220_file_list.txt)


Get
        Client                                  Server
        "get (filename)"
                                                (size of filename)
        "ACK"
                                                (contents of filename)


Put
        Client                                  Server
        "put (filename) (filesize)"
                                                "ACK"
        (contents of filename)

