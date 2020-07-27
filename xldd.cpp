/**
 * xldd -- A quick and dirty implementation of an ldd-like tool
 * that misses the load address, but works on all ELF files,
 * including those crosscompiled for another architecture
 *
 * (C) 2020 Bernhard "Bero" Rosenkränzer <bero@lindev.ch>
 *
 * Released under the GPLv3+
 */
#include <list>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

std::string locate(std::string const &lib) {
	std::list<std::string> paths = {
#if __WORDSIZE >= 64
		"/lib64",
		"/usr/lib64",
#endif
		"/lib",
		"/usr/lib"
	};
	char *llp = getenv("LD_LIBRARY_PATH");
	if(llp) {
		char *colon=strrchr(llp, ':');
		while(colon) {
			paths.push_front(colon+1);
			*colon=0;
			colon=strrchr(llp, ':');
		}
		paths.push_front(llp);
	}
	for(auto const &s : paths) {
		std::string p = s + "/" + lib;
		if(std::filesystem::exists(p))
			return p;
	}
	return "not found";
}

std::vector<std::string> deps(std::string const &binary, std::vector<std::string> exclude=std::vector<std::string>(), int indent=0) {
	std::vector<std::string> ret;
	FILE *f=popen((std::string("LANG=C LC_ALL=C LC_MESSAGES=C LINGUAS=C /usr/bin/llvm-readelf -d ") + binary).c_str(), "r");
	if(!f)
		return ret;
	char *line = nullptr;
	size_t len=0;
	ssize_t nread;
	while(!feof(f) && (nread = getline(&line, &len, f))) {
		if(!strstr(line, "(NEEDED)"))
			continue;
		char *sl = strstr(line, "Shared library: ");
		if(!sl)
			continue;
		sl += 17;
		char *end = strchr(sl, ']');
		if(!end)
			continue;
		*end=0;
		bool excluded = std::find(exclude.cbegin(), exclude.cend(), sl) != exclude.cend();
		if(!excluded) {
			ret.push_back(sl);
		}
	}
	if(line)
		free(line);
	pclose(f);
	for(int i=0; i<ret.size(); i++) {
		std::string s=ret.at(i);
		std::vector<std::string> xcl = ret;
		for(auto const &s : exclude)
			xcl.push_back(s);
		std::vector<std::string> d = deps(locate(s), xcl);
		for(auto const &sub : d)
			ret.push_back(sub);
	}
	return ret;
}

int main(int argc, char **argv) {
	if(argc <= 1) {
		std::cout << argv[0] << ": missing file arguments" << std::endl;
		return 1;
	}
	for(int i=1; i<argc; i++) {
		if(argc>=3)
			std::cout << argv[i] << ":" << std::endl;
		std::vector<std::string> d = deps(argv[i]);
		if(d.size()) {
			std::string dynld;
			std::cout << "\t" << "linux-vdso.so.1 (0x0)" << std::endl;
			for(auto const &s : d) {
				if(s.rfind("ld-linux") == 0)
					dynld=s;
				else
					std::cout << "\t" << s << " => " << locate(s) << " (0x0)" << std::endl;
			}
			if(dynld.length())
				std::cout << "\t" << locate(dynld) << " (0x0)" << std::endl;
		}
	}
	return 0;
}