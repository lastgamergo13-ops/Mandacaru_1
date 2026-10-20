#include "util/SystemInfo/SystemInfo.h"

uint64 ProcessorTime::work()
{
	return user + kernel;
}

uint64 ProcessorTime::total()
{
	return idle + user + kernel;
}

double ProcessorTime::Compare(ProcessorTime &last, ProcessorTime &now)
{
	auto dwork = now.work() - last.work();
	auto dtotal = now.total() - last.total();

	if (dtotal == 0 || now.total() < last.total())
		return 0.0; // sem dados novos (ou leitura invalida): evita 0/0 = "nan%" no overlay
	const double usage = (double)dwork / (double)dtotal;
	return usage > 1.0 ? 1.0 : usage;
}

uint32 GetProcessorCount()
{
	return std::thread::hardware_concurrency();
}

void QueryProcTime(ProcessorTime &out)
{
	uint64 now, user, kernel;
	QueryProcTime(now, user, kernel);

	out.idle = now - (user + kernel);
	out.kernel = kernel;
	out.user = user;
}