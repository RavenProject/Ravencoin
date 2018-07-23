#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>

std::string testfunc();
std::string exec(const char* cmd);

class Ipfs {
	public:
		std::string init();
		std::string start_daemon();
	private:
		std::string ipfs_path;
};