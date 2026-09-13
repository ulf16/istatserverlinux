/*
 *  Copyright 2016 Bjango Pty Ltd. All rights reserved.
 *  Copyright 2010 William Tisäter. All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *    1.  Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *    2.  Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *
 *    3.  The name of the copyright holder may not be used to endorse or promote
 *        products derived from this software without specific prior written
 *        permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER ``AS IS'' AND ANY
 *  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL WILLIAM TISÄTER BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "StatsProcesses.h"
#ifdef USE_PROCESSES_PROCFS
#include "LinuxProcStat.h"
#include <fstream>
#include <iterator>
#endif

using namespace std;


#ifdef USE_PROCESSES_AIX

void StatsProcesses::init()
{
	//aixEntitlement = 0.12;
}

void StatsProcesses::update(long long sampleID, double totalTicks)
{
	int treesize;
	pid_t firstproc = 0;
	if ((treesize = getprocs64(NULL, 0, NULL, 0, &firstproc, PID_MAX)) < 0) {
		return;
	}
	struct procentry64 *procs = (struct procentry64 *)malloc(treesize * sizeof (struct procentry64));;

	firstproc = 0;
	if ((treesize = getprocs64(procs, sizeof(struct procentry64), NULL, 0, &firstproc, treesize)) < 0) {
		free(procs);
		return;
	}

	threadCount = 0;

	double currentTime = get_current_time();

	for (int i = 0; i < treesize; i++)
	{
		pid_t pid = procs[i].pi_pid;
		if(pid == 0)
			continue;

		processProcess(pid, sampleID);

		for (vector<process_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
		{
			if((*cur).pid == pid)
			{
				struct psinfo psinfo;
				char buffer [BUFSIZ];

				sprintf (buffer, "/proc/%d/psinfo", (int) pid);

				int fd = open(buffer, O_RDONLY);

				if (fd < 0) {
					continue;
				}

				ssize_t len = pread(fd, &psinfo, sizeof (struct psinfo), 0);
				if (len != sizeof (struct psinfo))
				{
					close(fd);
					continue;
				}

				(*cur).exists = true;
				if((*cur).is_new == true)
				{
					sprintf((*cur).name, psinfo.pr_fname);
					(*cur).is_new = false;
				}

				double t = (double)(procs[i].pi_ru.ru_utime.tv_sec + procs[i].pi_ru.ru_stime.tv_sec);
				t += (double)(procs[i].pi_ru.ru_utime.tv_usec + procs[i].pi_ru.ru_stime.tv_usec) / NS_PER_SEC;
				
				if((*cur).cpuTime == 0)
				{
					(*cur).cpuTime = t;
					(*cur).lastClockTime = currentTime;
				}

				double timediff = currentTime - (*cur).lastClockTime;

				(*cur).cpu = ((t - (*cur).cpuTime) / timediff * 10000) / (aixEntitlement * 100);
				(*cur).threads = procs[i].pi_thcount;
				(*cur).memory = (uint64_t)(procs[i].pi_drss + procs[i].pi_trss) * (uint64_t)getpagesize();
				threadCount += procs[i].pi_thcount;

				(*cur).cpuTime = t;
				(*cur).lastClockTime = currentTime;

				close(fd);
			}
		}
	}

	free(procs);
}

#elif defined(USE_PROCESSES_PSINFO)

void StatsProcesses::init()
{
}

void StatsProcesses::update(long long sampleID, double totalTicks)
{
	DIR *dir;
	struct dirent *entry;

	if (!(dir = opendir("/proc")))
	{
		return;
	}

	while ((entry = readdir(dir)))
	{
		int pid = 0;

		if (!entry)
		{
			closedir(dir);
			return;
		}

		if (sscanf(entry->d_name, "%d", &pid) > 0)
		{
			processProcess(pid, sampleID);

			for (vector<process_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
			{
				if((*cur).pid == pid)
				{
					struct psinfo psinfo;
					char buffer [BUFSIZ];

					sprintf (buffer, "/proc/%d/psinfo", (int) pid);

   					int fd = open(buffer, O_RDONLY);

					if (fd < 0) {
						continue;
					}

   					ssize_t len = pread(fd, &psinfo, sizeof (struct psinfo), 0);
					if (len != sizeof (struct psinfo))
					{
						close(fd);
						continue;
					}

					(*cur).exists = true;
					if((*cur).is_new == true)
					{
						sprintf((*cur).name, psinfo.pr_fname);
						(*cur).is_new = false;
					}

					(*cur).cpu = (double)(psinfo.pr_pctcpu * 100.0f) / 0x8000;
					(*cur).memory = psinfo.pr_rssize * 1024;

					close(fd);
				}
			}
		}
	}

	closedir(dir);
}

#elif defined(USE_PROCESSES_KVM)

void StatsProcesses::init()
{

}
void StatsProcesses::update(long long sampleID, double totalTicks)
{
	#if defined(PROCESSES_KVM_NETBSD)
	struct kinfo_proc2 *p;
	#else
	struct kinfo_proc *p;
	#endif
	int n_processes;
	int i;

	#if defined(PROCESSES_KVM_NETBSD)
	p = kvm_getproc2(kd, KERN_PROC_ALL, 0, sizeof(kinfo_proc2), &n_processes);
	#elif defined(PROCESSES_KVM_OPENBSD)
	p = kvm_getprocs(kd, KERN_PROC_ALL, 0, sizeof(kinfo_proc), &n_processes);
	#elif defined(PROCESSES_KVM_DRAGONFLY)
	p = kvm_getprocs(kd, KERN_PROC_ALL, sizeof(kinfo_proc), &n_processes);
	#else
	p = kvm_getprocs(kd, KERN_PROC_PROC, 0, &n_processes);
	#endif

	for (i = 0; i < n_processes; i++) {
		#if defined(PROCESSES_KVM_DRAGONFLY)
		if (!((p[i].kp_flags & P_SYSTEM)) && p[i].kp_comm != NULL) {
		#elif defined(PROCESSES_KVM_OPENBSD) || defined(PROCESSES_KVM_NETBSD)
		if (!((p[i].p_flag & P_SYSTEM)) && p[i].p_comm != NULL) {
		#else
		if (p[i].ki_stat != 0) {
			#ifdef TDF_IDLETD
			if(p[i].ki_tdflags & TDF_IDLETD)
				continue;
			#endif
		#endif

		#if defined(PROCESSES_KVM_DRAGONFLY)
			int pid = p[i].kp_pid;
			struct kinfo_lwp lwp = p[i].kp_lwp;
		#elif defined(PROCESSES_KVM_OPENBSD) || defined(PROCESSES_KVM_NETBSD)
			int pid = p[i].p_pid;
		#else
			int pid = p[i].ki_pid;
		#endif
			processProcess(pid, sampleID);

			for (vector<process_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
			{
				if((*cur).pid == pid)
				{
					(*cur).exists = true;
					if((*cur).is_new == true)
					{
						#if defined(PROCESSES_KVM_DRAGONFLY)
						sprintf((*cur).name, "%s", p[i].kp_comm);
						#elif defined(PROCESSES_KVM_OPENBSD) || defined(PROCESSES_KVM_NETBSD)
						sprintf((*cur).name, "%s", p[i].p_comm);
						#else
						sprintf((*cur).name, "%s", p[i].ki_comm);
						#endif
						(*cur).is_new = false;
					}

					#if defined(PROCESSES_KVM_DRAGONFLY)
					(*cur).memory = (p[i].kp_vm_rssize * getpagesize());
					(*cur).cpu = (double)(100.0 * lwp.kl_pctcpu / FSCALE);
					#elif defined(PROCESSES_KVM_OPENBSD) || defined(PROCESSES_KVM_NETBSD)
					(*cur).memory = (p[i].p_vm_rssize * getpagesize());
					(*cur).cpu = (double)(100.0 * p[i].p_pctcpu / FSCALE);
					#else
					(*cur).memory = (p[i].ki_rssize * getpagesize());
					(*cur).cpu = (double)(100.0 * p[i].ki_pctcpu / FSCALE);
					#endif
				}
			}
		}
	}
}

#elif defined(USE_PROCESSES_PROCFS)

void StatsProcesses::init()
{

}

vector<string> StatsProcesses::componentsFromString(string input, char seperator)
{
	vector<string> components;
	stringstream ss(input);
	string tok;
  
	while(getline(ss, tok, seperator))
	{
		components.push_back(tok);
	}
	return components;
}

string StatsProcesses::nameFromCmd(int pid, string name)
{
	stringstream tmp;
	tmp << "/proc/" << pid << "/cmdline";

	int fd = open(tmp.str().c_str(), O_RDONLY);
	if (fd > 0) {
		char buffer[1024];
		ssize_t len;
		if ((len = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
			buffer[len] = '\0';
					
			int i;
			for (i = 0; i < len; i++) {
				if (buffer[i] == '\0')
					buffer[i] = ' ';
			}
		}
		close(fd);

		if(len == 0)
			return name;

		string cmd = string(buffer);

		vector<string> components = componentsFromString(cmd, ' ');
		if(components.size() > 0)
		{
			for (vector<string>::iterator cur = components.begin(); cur != components.end(); ++cur)
			{
				if((*cur).find(name) != std::string::npos)
				{
					vector<string> componentsn = componentsFromString((*cur), '/');
					if(componentsn.size() > 0)
						return componentsn.back();
					return (*cur);
				}
			}
		}
	}
	return name;
}

string StatsProcesses::nameFromStatus(int pid)
{

	stringstream tmp;
	tmp << "/proc/" << pid << "/status";

	std::ifstream input(tmp.str().c_str());
	std::string line;
	while (std::getline(input, line))
		if (line.compare(0, 5, "Name:") == 0) {
			size_t start = line.find_first_not_of(" \t", 5);
			return start == std::string::npos ? "" : line.substr(start);
		}
	return "";
}

void StatsProcesses::update(long long sampleID, double totalTicks)
{
	DIR *dir;
	struct dirent *entry;

	if (!(dir = opendir("/proc")))
	{
		return;
	}

	while ((entry = readdir(dir)))
	{
		int pid = 0;

		if (!entry)
		{
			break;
		}

		if (sscanf(entry->d_name, "%d", &pid) > 0)
		{
			processProcess(pid, sampleID);

			for (vector<process_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
			{
				if((*cur).pid == pid)
				{
					(*cur).exists = true;

					if((*cur).is_new == true)
					{
						string name = nameFromStatus(pid);
						if(name.length() == 15)
						{
							name = nameFromCmd(pid, name);
						}
						snprintf((*cur).name, sizeof((*cur).name), "%s", name.c_str());
						(*cur).is_new = false;
					}

					{
						stringstream tmp;
						tmp << "/proc/" << pid << "/stat";

						std::ifstream input(tmp.str().c_str());
						if (input)
						{
							std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
							istat::LinuxProcStat stat;
							unsigned long long memory = 0;
							if(!input.bad() && istat::parseLinuxProcStat(text, stat) && istat::residentBytes(stat, getpagesize(), memory))
							{
								double totalTime = (double)stat.userTicks + (double)stat.systemTicks;
								if((*cur).cpuTime == 0)
								{
									(*cur).cpuTime = totalTime;
									(*cur).lastClockTime = get_current_time();
								}

								double cpuTime = totalTime - (*cur).cpuTime;
								if (cpuTime < 0) cpuTime = 0;
								double clockTimeDifference = get_current_time() - (*cur).lastClockTime;

								if(clockTimeDifference > 0)
								{
									double procs = (double)sysconf(_SC_NPROCESSORS_ONLN);
									if(procs < 1)
										procs = 1;
									(*cur).cpu = (((cpuTime / (double)sysconf(_SC_CLK_TCK)) / clockTimeDifference) * 100) / procs;
								}
								else
								{
									(*cur).cpu = 0;
								}

								threadCount += stat.threads;
								(*cur).threads = stat.threads;
								(*cur).memory = memory;
								(*cur).cpuTime = totalTime;
								(*cur).lastClockTime = get_current_time();
							}
							else
								(*cur).exists = false;
						}
						else
							(*cur).exists = false;
					}
					
					// /proc/pid/io requires root access which we usually dont run with
					/*
					{
						stringstream tmp;
						tmp << "/proc/" << pid << "/io";

						FILE * fp = NULL;
	
						if ((fp = fopen(tmp.str().c_str(), "r")))
						{
							char buf[1024];
							unsigned long long totalRead = 0;
							unsigned long long totalWrite = 0;
							while (fgets(buf, sizeof(buf), fp))
							{
								sscanf(buf, "read_bytes: %llu", &totalRead);
								sscanf(buf, "write_bytes: %llu", &totalWrite);
							}

							if((*cur).io_read_total == 0)
							{
								(*cur).io_read_total = totalRead;
							}

							if((*cur).io_write_total == 0)
							{
								(*cur).io_write_total = totalWrite;
							}

							(*cur).io_read = totalRead - (*cur).io_read_total;
							(*cur).io_write = totalWrite - (*cur).io_write_total;

							(*cur).io_read_total = totalRead;
							(*cur).io_write_total = totalWrite;

							fclose(fp);
						}
					}*/
				}
			}
		}
	}

	closedir(dir);
}

#elif defined(USE_PROCESSES_DARWIN)

void StatsProcesses::init()
{
}

void StatsProcesses::update(long long sampleID, double totalTicks)
{
	(void)totalTicks;

	static double nextAllowed = 0.0;
	double now = get_current_time();
	if (now < nextAllowed)
	{
		processCount = (long)_items.size();
		for (vector<process_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
			(*cur).exists = true;
		return;
	}
	nextAllowed = now + 1.0;

	static bool chdirFixed = false;
	if (!chdirFixed)
	{
		(void)chdir("/");
		chdirFixed = true;
	}

	FILE *fp = popen("/bin/ps -axo pid=,pcpu=,rss=,ucomm= -r", "r");
	if (fp == NULL)
		return;

	char line[2048];
	while (fgets(line, sizeof(line), fp))
	{
		int pid = 0;
		double cpu = 0.0;
		unsigned long rss = 0;
		char name[1024] = {0};

		if (sscanf(line, "%d %lf %lu %1023[^\n]", &pid, &cpu, &rss, name) < 4 || pid <= 0)
			continue;

		size_t nameLength = strlen(name);
		while (nameLength > 0 && (name[nameLength - 1] == ' ' || name[nameLength - 1] == '\t' || name[nameLength - 1] == '\r' || name[nameLength - 1] == '\n'))
			name[--nameLength] = '\0';

		processProcess(pid, sampleID);

		for (vector<process_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
		{
			if ((*cur).pid == pid)
			{
				(*cur).exists = true;
				if ((*cur).is_new == true)
				{
					snprintf((*cur).name, sizeof((*cur).name), "%s", name);
					(*cur).is_new = false;
				}
				(*cur).cpu = cpu;
				(*cur).memory = (unsigned long long)rss * 1024ULL;
				(*cur).threads = 0;
				break;
			}
		}
	}

	pclose(fp);
}

#else

void StatsProcesses::init()
{

}

void StatsProcesses::update(long long sampleID, double totalTicks)
{

}

#endif

void StatsProcesses::createProcess(int pid)
{
	if(_items.size() > 0)
	{
		for (vector<process_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
		{
				process_info process = *cur;
				if(process.pid == pid){
					return;
				}
		}
	}

	process_info process;
	process.cpu = 0;
	process.memory = 0;
	process.is_new = true;
	process.pid = pid;
	process.cpuTime = 0;
	process.io_read = 0;
	process.io_write = 0;
	process.exists = true;
	_items.insert(_items.begin(), process);	
}

void StatsProcesses::processProcess(int pid, long long sampleID)
{
	processCount++;
	createProcess(pid);
}

void StatsProcesses::prepareUpdate()
{
	threadCount = 0;
	processCount = 0;

	if(_items.size() > 0)
	{
		for (vector<process_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
		{
			(*cur).exists = false;
		}
	}
}

void StatsProcesses::finishUpdate()
{
	if(_items.size() > 0)
	{
		for (vector<process_info>::iterator i = _items.begin(); i != _items.end(); ) {
  			if (i->exists == false) {
    			i = _items.erase(i);
  			} else {
    			++i;
			}
		}
	}
}
