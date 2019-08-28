# 使用指南

1. 预先安装boost, 使用命令`apt install libboost-dev-all`
1. 预先安装fc
    ```bash
    git clone https://github.com/EOSIO/fc.git --recursive
    cd fc
    mkdir build
    cd build
    cmake ..
    make -j 12
    sudo make install
     ```
1. 使用此项目中的examples/main.cpp运行服务器端
    1. 这里有一点小问题，需要给代码打一个tag才能编译，不然在执行make命令时会报错`get describe failed`
    1. git tag -s 'test' -m 'test'
    1. git checkout test
    1. mkdir build;cd build;cmake ..;make;
1. 使用`python client.py`可以向服务器端快速的发送请求
