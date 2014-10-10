cd ..
make clean
make
cd testsuite
test="test$1.in"
./sdriver.pl -t $test -s .././tsh
