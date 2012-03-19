set -e
set -u

libwebsockets_dir=$HOME/git/libwebsockets/
minisat_dir=$HOME/git/minisat/

g++ -Wall -std=c++0x -I$libwebsockets_dir/lib -L$libwebsockets_dir/lib/.libs -I$minisat_dir -L$minisat_dir/build/release/lib -o minisat-www main.cc -lwebsockets -lminisat -lpthread -lz
