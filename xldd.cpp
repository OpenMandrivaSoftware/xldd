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
#ifndef USE_EXTERNAL_ELF_PARSER
extern "C" {
#include <gelf.h>
#include <fcntl.h>
#include <unistd.h>
}
#endif

std::string locate(std::string const &lib, std::vector<std::string> const &rpath=std::vector<std::string>(), std::vector<std::string> const &runpath=std::vector<std::string>()) {
	std::list<std::string> paths = {
#if __WORDSIZE >= 64
		"/lib64",
		"/usr/lib64",
#endif
		"/lib",
		"/usr/lib"
	};
	for(auto const &s : runpath)
		paths.push_front(s);
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
	for(auto const &s : rpath)
		paths.push_front(s);
	for(auto const &s : paths) {
		std::string p = s + "/" + lib;
		if(std::filesystem::exists(p))
			return p;
	}
	return "not found";
}

typedef std::pair<std::string,std::string> library;

std::vector<library> deps(std::string const &binary, std::vector<std::string> exclude=std::vector<std::string>(), int indent=0) {
	std::vector<library> ret;
	std::vector<std::string> rpath, runpath;

#ifndef USE_EXTERNAL_ELF_PARSER
	int fd = open(binary.c_str(), O_RDONLY);
	Elf *elf = elf_begin(fd, ELF_C_READ, 0L);
	Elf_Scn *scn = nullptr;
	while((scn=elf_nextscn(elf, scn))) {
		GElf_Shdr shdr = {};
		gelf_getshdr(scn, &shdr);
		if(shdr.sh_type == SHT_DYNAMIC) {
			Elf_Data *data = NULL;
			data = elf_getdata(scn, data);
			size_t sh_entsize = gelf_fsize(elf, ELF_T_DYN, 1, EV_CURRENT);
			for(size_t i = 0; i<shdr.sh_size/sh_entsize; i++) {
				GElf_Dyn dyn = {};
				gelf_getdyn(data, i, &dyn);
				if(dyn.d_tag == DT_NEEDED) {
					std::string sl = elf_strptr(elf, shdr.sh_link, dyn.d_un.d_val);
					bool excluded = std::find(exclude.cbegin(), exclude.cend(), sl) != exclude.cend();
					if(!excluded)
						ret.push_back(std::make_pair(sl, std::string()));
				} else if(dyn.d_tag == DT_RPATH || dyn.d_tag == DT_RUNPATH) {
					std::string rp = elf_strptr(elf, shdr.sh_link, dyn.d_un.d_val);
					if(dyn.d_tag == DT_RPATH)
						rpath.push_back(rp);
					else
						runpath.push_back(rp);
				}
			}
		}
	}
	elf_end(elf);
	close(fd);
#else
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
			ret.push_back(std::make_pair<std::string,std::string>(sl, ""));
		}
	}
	if(line)
		free(line);
	pclose(f);
#endif
	for(int i=0; i<ret.size(); i++) {
		ret.at(i).second=locate(ret.at(i).first, rpath, runpath);
	}
	for(int i=0; i<ret.size(); i++) {
		std::string s=ret.at(i).first;
		std::vector<std::string> xcl;
		for(auto const &d : ret)
			xcl.push_back(d.first);
		for(auto const &x : exclude)
			xcl.push_back(x);
		std::vector<library> d = deps(ret.at(i).second, xcl);
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
#ifndef USE_EXTERNAL_ELF_PARSER
	elf_version(EV_CURRENT);
#endif
	for(int i=1; i<argc; i++) {
		if(argc>=3)
			std::cout << argv[i] << ":" << std::endl;
		std::vector<library> d = deps(argv[i]);
		if(d.size()) {
			std::string dynld;
			std::cout << "\t" << "linux-vdso.so.1 (0x0)" << std::endl;
			for(auto const &s : d) {
				if(s.first.rfind("ld-linux") == 0)
					dynld=s.first;
				else
					std::cout << "\t" << s.first << " => " << s.second << " (0x0)" << std::endl;
			}
			if(dynld.length())
				std::cout << "\t" << locate(dynld) << " (0x0)" << std::endl;
		}
	}
	return 0;
}
