/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <cstdlib>

#include "CPUInfo.h"
#include "utils/Temperature.h"
#include <string>
#include <string.h>

#if defined(TARGET_DARWIN)
#include <sys/types.h>
#include <sys/sysctl.h>
#if defined (TARGET_DARWIN_IOS)
#include <mach-o/arch.h>
#endif // defined (TARGET_DARWIN_IOS)
#ifdef TARGET_DARWIN_OSX
#include "platform/darwin/osx/smc.h"
#endif
#include "linux/LinuxResourceCounter.h"
static CLinuxResourceCounter static_resourceCounter;
#endif

#if defined(TARGET_FREEBSD)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#endif

#if defined(TARGET_LINUX) && defined(__ARM_NEON__) && !defined(TARGET_ANDROID)
#include <fcntl.h>
#include <unistd.h>
#include <elf.h>
#include <linux/auxvec.h>
#include <asm/hwcap.h>
#endif

#if defined(TARGET_ANDROID)
#include "platform/android/activity/AndroidFeatures.h"
#endif

#ifdef TARGET_POSIX
#include "settings/AdvancedSettings.h"
#endif

#include "utils/StringUtils.h"

// In milliseconds
#define MINIMUM_TIME_BETWEEN_READS 500

CCPUInfo::CCPUInfo(void)
{
#ifdef TARGET_POSIX
  m_fProcStat = m_fProcTemperature = m_fCPUFreq = NULL;
  m_cpuInfoForFreq = false;
#endif
  m_lastUsedPercentage = 0;
  m_cpuFeatures = 0;

#if defined(TARGET_DARWIN)
  size_t len = 4;
  std::string cpuVendor;
  
  // The number of cores.
  if (sysctlbyname("hw.activecpu", &m_cpuCount, &len, NULL, 0) == -1)
      m_cpuCount = 1;

  // The model.
#if defined (TARGET_DARWIN_IOS)
  const NXArchInfo *info = NXGetLocalArchInfo();
  if (info != NULL)
    m_cpuModel = info->description;
#else
  // NXGetLocalArchInfo is ugly for intel so keep using this method
  char buffer[512];
  len = 512;
  if (sysctlbyname("machdep.cpu.brand_string", &buffer, &len, NULL, 0) == 0)
    m_cpuModel = buffer;

  // The CPU vendor
  len = 512;
  if (sysctlbyname("machdep.cpu.vendor", &buffer, &len, NULL, 0) == 0)
    cpuVendor = buffer;
  
#endif
  // Go through each core.
  for (int i=0; i<m_cpuCount; i++)
  {
    CoreInfo core;
    core.m_id = i;
    core.m_strModel = m_cpuModel;
    core.m_strVendor = cpuVendor;
    m_cores[core.m_id] = core;
  }
#elif defined(TARGET_FREEBSD)
  size_t len;
  int i;
  char cpumodel[512];

  len = sizeof(m_cpuCount);
  if (sysctlbyname("hw.ncpu", &m_cpuCount, &len, NULL, 0) != 0)
    m_cpuCount = 1;

  len = sizeof(cpumodel);
  if (sysctlbyname("hw.model", &cpumodel, &len, NULL, 0) != 0)
    (void)strncpy(cpumodel, "Unknown", 8);
  m_cpuModel = cpumodel;

  for (i = 0; i < m_cpuCount; i++)
  {
    CoreInfo core;
    core.m_id = i;
    core.m_strModel = m_cpuModel;
    m_cores[core.m_id] = core;
  }
#else
  m_fProcStat = fopen("/proc/stat", "r");
  m_fProcTemperature = fopen("/proc/acpi/thermal_zone/THM0/temperature", "r");
  if (m_fProcTemperature == NULL)
    m_fProcTemperature = fopen("/proc/acpi/thermal_zone/THRM/temperature", "r");
  if (m_fProcTemperature == NULL)
    m_fProcTemperature = fopen("/proc/acpi/thermal_zone/THR0/temperature", "r");
  if (m_fProcTemperature == NULL)
    m_fProcTemperature = fopen("/proc/acpi/thermal_zone/TZ0/temperature", "r");
  // read from the new location of the temperature data on new kernels, 2.6.39, 3.0 etc
  if (m_fProcTemperature == NULL)   
    m_fProcTemperature = fopen("/sys/class/hwmon/hwmon0/temp1_input", "r");
  if (m_fProcTemperature == NULL)   
    m_fProcTemperature = fopen("/sys/class/thermal/thermal_zone0/temp", "r");  // On Raspberry PIs

  m_fCPUFreq = fopen ("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
  if (!m_fCPUFreq)
  {
    m_cpuInfoForFreq = true;
    m_fCPUFreq = fopen("/proc/cpuinfo", "r");
  }
  else
    m_cpuInfoForFreq = false;


  FILE* fCPUInfo = fopen("/proc/cpuinfo", "r");
  m_cpuCount = 0;
  if (fCPUInfo)
  {
    char buffer[512];

    int nCurrId = 0;
    while (fgets(buffer, sizeof(buffer), fCPUInfo))
    {
      if (strncmp(buffer, "processor", strlen("processor"))==0)
      {
        char *needle = strstr(buffer, ":");
        if (needle)
        {
          CoreInfo core;
          core.m_id = atoi(needle+2);
          nCurrId = core.m_id;
          m_cores[core.m_id] = core;
        }
        m_cpuCount++;
      }
      else if (strncmp(buffer, "vendor_id", strlen("vendor_id"))==0)
      {
        char *needle = strstr(buffer, ":");
        if (needle && strlen(needle)>3)
        {
          needle+=2;
          m_cores[nCurrId].m_strVendor = needle;
          StringUtils::Trim(m_cores[nCurrId].m_strVendor);
        }
      }
      else if (strncmp(buffer, "Processor", strlen("Processor"))==0)
      {
        char *needle = strstr(buffer, ":");
        if (needle && strlen(needle)>3)
        {
          needle+=2;
          m_cpuModel = needle;
          m_cores[nCurrId].m_strModel = m_cpuModel;
          StringUtils::Trim(m_cores[nCurrId].m_strModel);
        }
      }
      else if (strncmp(buffer, "BogoMIPS", strlen("BogoMIPS"))==0)
      {
        char *needle = strstr(buffer, ":");
        if (needle && strlen(needle)>3)
        {
          needle+=2;
          m_cpuBogoMips = needle;
          m_cores[nCurrId].m_strBogoMips = m_cpuBogoMips;
          StringUtils::Trim(m_cores[nCurrId].m_strBogoMips);
        }
      }
      else if (strncmp(buffer, "Hardware", strlen("Hardware"))==0)
      {
        char *needle = strstr(buffer, ":");
        if (needle && strlen(needle)>3)
        {
          needle+=2;
          m_cpuHardware = needle;
          m_cores[nCurrId].m_strHardware = m_cpuHardware;
          StringUtils::Trim(m_cores[nCurrId].m_strHardware);
        }
      }
      else if (strncmp(buffer, "Revision", strlen("Revision"))==0)
      {
        char *needle = strstr(buffer, ":");
        if (needle && strlen(needle)>3)
        {
          needle+=2;
          m_cpuRevision = needle;
          m_cores[nCurrId].m_strRevision = m_cpuRevision;
          StringUtils::Trim(m_cores[nCurrId].m_strRevision);
        }
      }
      else if (strncmp(buffer, "Serial", strlen("Serial"))==0)
      {
        char *needle = strstr(buffer, ":");
        if (needle && strlen(needle)>3)
        {
          needle+=2;
          m_cpuSerial = needle;
          m_cores[nCurrId].m_strSerial = m_cpuSerial;
          StringUtils::Trim(m_cores[nCurrId].m_strSerial);
        }
      }
      else if (strncmp(buffer, "model name", strlen("model name"))==0)
      {
        char *needle = strstr(buffer, ":");
        if (needle && strlen(needle)>3)
        {
          needle+=2;
          m_cpuModel = needle;
          m_cores[nCurrId].m_strModel = m_cpuModel;
          StringUtils::Trim(m_cores[nCurrId].m_strModel);
        }
      }
      else if (strncmp(buffer, "flags", 5) == 0)
      {
        char* needle = strchr(buffer, ':');
        if (needle)
        {
          char* tok = NULL,
              * save;
          needle++;
          tok = strtok_r(needle, " ", &save);
          while (tok)
          {
            if (0 == strcmp(tok, "mmx"))
              m_cpuFeatures |= CPU_FEATURE_MMX;
            else if (0 == strcmp(tok, "mmxext"))
              m_cpuFeatures |= CPU_FEATURE_MMX2;
            else if (0 == strcmp(tok, "sse"))
              m_cpuFeatures |= CPU_FEATURE_SSE;
            else if (0 == strcmp(tok, "sse2"))
              m_cpuFeatures |= CPU_FEATURE_SSE2;
            else if (0 == strcmp(tok, "sse3"))
              m_cpuFeatures |= CPU_FEATURE_SSE3;
            else if (0 == strcmp(tok, "ssse3"))
              m_cpuFeatures |= CPU_FEATURE_SSSE3;
            else if (0 == strcmp(tok, "sse4_1"))
              m_cpuFeatures |= CPU_FEATURE_SSE4;
            else if (0 == strcmp(tok, "sse4_2"))
              m_cpuFeatures |= CPU_FEATURE_SSE42;
            else if (0 == strcmp(tok, "3dnow"))
              m_cpuFeatures |= CPU_FEATURE_3DNOW;
            else if (0 == strcmp(tok, "3dnowext"))
              m_cpuFeatures |= CPU_FEATURE_3DNOWEXT;
            tok = strtok_r(NULL, " ", &save);
          }
        }
      }
    }
    fclose(fCPUInfo);
    //  /proc/cpuinfo is not reliable on some Android platforms
    //  At least we should get the correct cpu count for multithreaded decoding
#if defined(TARGET_ANDROID)
    if (CAndroidFeatures::GetCPUCount() > m_cpuCount)
    {
      m_cpuCount = CAndroidFeatures::GetCPUCount();
    }
#endif
  }
  else
  {
    m_cpuCount = 1;
    m_cpuModel = "Unknown";
  }

#endif
  StringUtils::Replace(m_cpuModel, '\r', ' ');
  StringUtils::Replace(m_cpuModel, '\n', ' ');
  StringUtils::RemoveDuplicatedSpacesAndTabs(m_cpuModel);
  StringUtils::Trim(m_cpuModel);

  /* Set some default for empty string variables */
  if (m_cpuBogoMips.empty())
    m_cpuBogoMips = "N/A";
  if (m_cpuHardware.empty())
    m_cpuHardware = "N/A";
  if (m_cpuRevision.empty())
    m_cpuRevision = "N/A";
  if (m_cpuSerial.empty())
    m_cpuSerial = "N/A";

  readProcStat(m_userTicks, m_niceTicks, m_systemTicks, m_idleTicks, m_ioTicks);
  m_nextUsedReadTime.Set(MINIMUM_TIME_BETWEEN_READS);

  ReadCPUFeatures();

  // Set MMX2 when SSE is present as SSE is a superset of MMX2 and Intel doesn't set the MMX2 cap
  if (m_cpuFeatures & CPU_FEATURE_SSE)
    m_cpuFeatures |= CPU_FEATURE_MMX2;

  if (HasNeon())
    m_cpuFeatures |= CPU_FEATURE_NEON;

}

CCPUInfo::~CCPUInfo()
{
#ifdef TARGET_POSIX
  if (m_fProcStat != NULL)
    fclose(m_fProcStat);

  if (m_fProcTemperature != NULL)
    fclose(m_fProcTemperature);

  if (m_fCPUFreq != NULL)
    fclose(m_fCPUFreq);
#endif
}

int CCPUInfo::getUsedPercentage()
{
  if (!m_nextUsedReadTime.IsTimePast())
    return m_lastUsedPercentage;

  int result;
#if defined(TARGET_DARWIN)
  result = static_resourceCounter.GetCPUUsage();
#else
  unsigned long long userTicks;
  unsigned long long niceTicks;
  unsigned long long systemTicks;
  unsigned long long idleTicks;
  unsigned long long ioTicks;

  if (!readProcStat(userTicks, niceTicks, systemTicks, idleTicks, ioTicks))
    return m_lastUsedPercentage;

  userTicks -= m_userTicks;
  niceTicks -= m_niceTicks;
  systemTicks -= m_systemTicks;
  idleTicks -= m_idleTicks;
  ioTicks -= m_ioTicks;

  if(userTicks + niceTicks + systemTicks + idleTicks + ioTicks == 0)
    return m_lastUsedPercentage;
  result = (int) (double(userTicks + niceTicks + systemTicks) * 100.0 / double(userTicks + niceTicks + systemTicks + idleTicks + ioTicks) + 0.5);

  m_userTicks += userTicks;
  m_niceTicks += niceTicks;
  m_systemTicks += systemTicks;
  m_idleTicks += idleTicks;
  m_ioTicks += ioTicks;

#endif
  m_lastUsedPercentage = result;
  m_nextUsedReadTime.Set(MINIMUM_TIME_BETWEEN_READS);

  return result;
}

float CCPUInfo::getCPUFrequency()
{
  // Get CPU frequency, scaled to MHz.
#if defined(TARGET_DARWIN)
  long long hz = 0;
  size_t len = sizeof(hz);
  if (sysctlbyname("hw.cpufrequency", &hz, &len, NULL, 0) == -1)
    return 0.f;
  return hz / 1000000.0;
#elif defined(TARGET_FREEBSD)
  int hz = 0;
  size_t len = sizeof(hz);
  if (sysctlbyname("dev.cpu.0.freq", &hz, &len, NULL, 0) != 0)
    hz = 0;
  return (float)hz;
#else
  int value = 0;
  if (m_fCPUFreq && !m_cpuInfoForFreq)
  {
    rewind(m_fCPUFreq);
    fflush(m_fCPUFreq);
    fscanf(m_fCPUFreq, "%d", &value);
    value /= 1000.0;
  }
  if (m_fCPUFreq && m_cpuInfoForFreq)
  {
    rewind(m_fCPUFreq);
    fflush(m_fCPUFreq);
    float mhz, avg=0.0;
    int n, cpus=0;
    while(EOF!=(n=fscanf(m_fCPUFreq," MHz : %f ", &mhz)))
    {
      if (n>0) {
        cpus++;
        avg += mhz;
      }
      fscanf(m_fCPUFreq,"%*s");
    }

    if (cpus > 0)
      value = avg/cpus;
  }
  return value;
#endif
}

bool CCPUInfo::getTemperature(CTemperature& temperature)
{
  int         value = 0;
  char        scale = 0;
  
#ifdef TARGET_POSIX
#if defined(TARGET_DARWIN_OSX)
  value = SMCGetTemperature(SMC_KEY_CPU_TEMP);
  scale = 'c';
#else
  int         ret   = 0;
  FILE        *p    = NULL;
  std::string  cmd   = g_advancedSettings.m_cpuTempCmd;

  temperature.SetValid(false);

  if (cmd.empty() && m_fProcTemperature == NULL)
    return false;

  if (!cmd.empty())
  {
    p = popen (cmd.c_str(), "r");
    if (p)
    {
      ret = fscanf(p, "%d %c", &value, &scale);
      pclose(p);
    }
  }
  else
  {
    // procfs is deprecated in the linux kernel, we should move away from
    // using it for temperature data.  It doesn't seem that sysfs has a
    // general enough interface to bother implementing ATM.
    
    rewind(m_fProcTemperature);
    fflush(m_fProcTemperature);
    ret = fscanf(m_fProcTemperature, "temperature: %d %c", &value, &scale);
    
    // read from the temperature file of the new kernels
    if (!ret)
    {
      ret = fscanf(m_fProcTemperature, "%d", &value);
      value = value / 1000;
      scale = 'c';
      ret++;
    }
  }

  if (ret != 2)
    return false; 
#endif
#endif // TARGET_POSIX

  if (scale == 'C' || scale == 'c')
    temperature = CTemperature::CreateFromCelsius(value);
  else if (scale == 'F' || scale == 'f')
    temperature = CTemperature::CreateFromFahrenheit(value);
  else
    return false;
  
  return true;
}

bool CCPUInfo::HasCoreId(int nCoreId) const
{
  std::map<int, CoreInfo>::const_iterator iter = m_cores.find(nCoreId);
  if (iter != m_cores.end())
    return true;
  return false;
}

const CoreInfo &CCPUInfo::GetCoreInfo(int nCoreId)
{
  std::map<int, CoreInfo>::iterator iter = m_cores.find(nCoreId);
  if (iter != m_cores.end())
    return iter->second;

  static CoreInfo dummy;
  return dummy;
}

bool CCPUInfo::readProcStat(unsigned long long& user, unsigned long long& nice,
    unsigned long long& system, unsigned long long& idle, unsigned long long& io)
{
#if defined(TARGET_FREEBSD)
  long *cptimes;
  size_t len;
  int i;

  len = sizeof(long) * 32 * CPUSTATES;
  if (sysctlbyname("kern.cp_times", NULL, &len, NULL, 0) != 0)
    return false;
  cptimes = (long*)malloc(len);
  if (cptimes == NULL)
    return false;
  if (sysctlbyname("kern.cp_times", cptimes, &len, NULL, 0) != 0)
  {
    free(cptimes);
    return false;
  }
  user = 0;
  nice = 0;
  system = 0;
  idle = 0;
  io = 0;
  for (i = 0; i < m_cpuCount; i++)
  {
    long coreUser, coreNice, coreSystem, coreIdle, coreIO;
    double total;

    coreUser   = cptimes[i * CPUSTATES + CP_USER];
    coreNice   = cptimes[i * CPUSTATES + CP_NICE];
    coreSystem = cptimes[i * CPUSTATES + CP_SYS];
    coreIO     = cptimes[i * CPUSTATES + CP_INTR];
    coreIdle   = cptimes[i * CPUSTATES + CP_IDLE];

    std::map<int, CoreInfo>::iterator iter = m_cores.find(i);
    if (iter != m_cores.end())
    {
      coreUser -= iter->second.m_user;
      coreNice -= iter->second.m_nice;
      coreSystem -= iter->second.m_system;
      coreIdle -= iter->second.m_idle;
      coreIO -= iter->second.m_io;

      total = (double)(coreUser + coreNice + coreSystem + coreIdle + coreIO);
      if(total != 0.0f)
        iter->second.m_fPct = ((double)(coreUser + coreNice + coreSystem) * 100.0) / total;
      else
        iter->second.m_fPct = 0.0f;

      iter->second.m_user += coreUser;
      iter->second.m_nice += coreNice;
      iter->second.m_system += coreSystem;
      iter->second.m_idle += coreIdle;
      iter->second.m_io += coreIO;

      user   += coreUser;
      nice   += coreNice;
      system += coreSystem;
      idle   += coreIdle;
      io     += coreIO;
    }
  }
  free(cptimes);
#else
  if (m_fProcStat == NULL)
    return false;

#ifdef TARGET_ANDROID
  // Just another (vanilla) NDK quirk:
  // rewind + fflush do not actually flush the buffers,
  // the same initial content is returned rather than re-read
  fclose(m_fProcStat);
  m_fProcStat = fopen("/proc/stat", "r");
#else
  rewind(m_fProcStat);
  fflush(m_fProcStat);
#endif

  char buf[256];
  if (!fgets(buf, sizeof(buf), m_fProcStat))
    return false;

  int num = sscanf(buf, "cpu %llu %llu %llu %llu %llu %*s\n", &user, &nice, &system, &idle, &io);
  if (num < 5)
    io = 0;

  // zero out cpu percents, cpu's can idle and disappear.
  for (int i = 0; i < m_cpuCount; i++)
  {
    std::map<int, CoreInfo>::iterator iter = m_cores.find(i);
    if (iter != m_cores.end())
      iter->second.m_fPct = 0.0;
  }

  while (fgets(buf, sizeof(buf), m_fProcStat) && num >= 4)
  {
    unsigned long long coreUser, coreNice, coreSystem, coreIdle, coreIO;
    int nCpu=0;
    num = sscanf(buf, "cpu%d %llu %llu %llu %llu %llu %*s\n", &nCpu, &coreUser, &coreNice, &coreSystem, &coreIdle, &coreIO);
    if (num < 6)
      coreIO = 0;

    std::map<int, CoreInfo>::iterator iter = m_cores.find(nCpu);
    if (num > 4 && iter != m_cores.end())
    {
      coreUser -= iter->second.m_user;
      coreNice -= iter->second.m_nice;
      coreSystem -= iter->second.m_system;
      coreIdle -= iter->second.m_idle;
      coreIO -= iter->second.m_io;

      double total = (double)(coreUser + coreNice + coreSystem + coreIdle + coreIO);
      if(total == 0.0f)
        iter->second.m_fPct = 0.0f;
      else
        iter->second.m_fPct = ((double)(coreUser + coreNice + coreSystem) * 100.0) / total;

      iter->second.m_user += coreUser;
      iter->second.m_nice += coreNice;
      iter->second.m_system += coreSystem;
      iter->second.m_idle += coreIdle;
      iter->second.m_io += coreIO;
    }
  }
#endif

  return true;
}

std::string CCPUInfo::GetCoresUsageString() const
{
  std::string strCores;
  for (std::map<int, CoreInfo>::const_iterator it = m_cores.begin(); it != m_cores.end(); ++it)
  {
    if (!strCores.empty())
      strCores += ' ';
    if (it->second.m_fPct < 10.0)
      strCores += StringUtils::Format("CPU%d: %1.1f%%", it->first, it->second.m_fPct);
    else
      strCores += StringUtils::Format("CPU%d: %3.0f%%", it->first, it->second.m_fPct);
  }

  return strCores;
}

void CCPUInfo::ReadCPUFeatures()
{
#if defined(TARGET_DARWIN)
  #if defined(TARGET_DARWIN_IOS)
  #else
    size_t len = 512 - 1; // '-1' for trailing space
    char buffer[512] ={0};

    if (sysctlbyname("machdep.cpu.features", &buffer, &len, NULL, 0) == 0)
    {
      strcat(buffer, " ");
      if (strstr(buffer,"MMX "))
        m_cpuFeatures |= CPU_FEATURE_MMX;
      if (strstr(buffer,"MMXEXT "))
        m_cpuFeatures |= CPU_FEATURE_MMX2;
      if (strstr(buffer,"SSE "))
        m_cpuFeatures |= CPU_FEATURE_SSE;
      if (strstr(buffer,"SSE2 "))
        m_cpuFeatures |= CPU_FEATURE_SSE2;
      if (strstr(buffer,"SSE3 "))
        m_cpuFeatures |= CPU_FEATURE_SSE3;
      if (strstr(buffer,"SSSE3 "))
        m_cpuFeatures |= CPU_FEATURE_SSSE3;
      if (strstr(buffer,"SSE4.1 "))
        m_cpuFeatures |= CPU_FEATURE_SSE4;
      if (strstr(buffer,"SSE4.2 "))
        m_cpuFeatures |= CPU_FEATURE_SSE42;
      if (strstr(buffer,"3DNOW "))
        m_cpuFeatures |= CPU_FEATURE_3DNOW;
      if (strstr(buffer,"3DNOWEXT "))
       m_cpuFeatures |= CPU_FEATURE_3DNOWEXT;
    }
    else
      m_cpuFeatures |= CPU_FEATURE_MMX;
  #endif
#elif defined(LINUX)
// empty on purpose, the implementation is in the constructor
#elif !defined(__arm__) && !defined(__arm64__) && !defined(__aarch64__)
  m_cpuFeatures |= CPU_FEATURE_MMX;
#endif
}

bool CCPUInfo::HasNeon()
{
  static int has_neon = -1;
#if defined (TARGET_ANDROID)
  if (has_neon == -1)
    has_neon = (CAndroidFeatures::HasNeon()) ? 1 : 0;

#elif defined(TARGET_DARWIN_IOS)
  has_neon = 1;

#elif defined(TARGET_LINUX) && defined(__ARM_NEON__)
  if (has_neon == -1)
  {
    has_neon = 0;
    // why are we not looking at the Features in
    // /proc/cpuinfo for neon ?
    int fd = open("/proc/self/auxv", O_RDONLY);
    if (fd >= 0)
    {
      Elf32_auxv_t auxv;
      while (read(fd, &auxv, sizeof(Elf32_auxv_t)) == sizeof(Elf32_auxv_t))
      {
        if (auxv.a_type == AT_HWCAP)
        {
          has_neon = (auxv.a_un.a_val & HWCAP_NEON) ? 1 : 0;
          break;
        }
      }
      close(fd);
    }
  }

#endif

  return has_neon == 1;
}

CCPUInfo g_cpuInfo;
