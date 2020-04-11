# dependence

seastar

    git clone https://github.com/scylladb/seastar.git
    cd seastar
	sudo ./install-dependencies.sh
	./configure.py --mode=release --enable-dpdk --enable-hwloc --without-tests --without-apps --without-demos --cook fmt --cook Protobuf
	ninja -C build/release