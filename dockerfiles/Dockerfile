FROM centos:7
MAINTAINER pyama86 <www.kazu.com@gmail.com>
RUN yum install -y sudo
RUN curl -fsSL https://repo.stns.jp/scripts/yum-repo.sh | sh
RUN yum install -q -y glibc gcc make wget bzip2 git vim epel-release clang stns-v2 initscripts nc libasan valgrind
RUN mkdir /stns
WORKDIR /stns
