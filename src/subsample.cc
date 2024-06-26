/*

  VSEARCH: a versatile open source tool for metagenomics

  Copyright (C) 2014-2024, Torbjorn Rognes, Frederic Mahe and Tomas Flouri
  All rights reserved.

  Contact: Torbjorn Rognes <torognes@ifi.uio.no>,
  Department of Informatics, University of Oslo,
  PO Box 1080 Blindern, NO-0316 Oslo, Norway

  This software is dual-licensed and available under a choice
  of one of two licenses, either under the terms of the GNU
  General Public License version 3 or the BSD 2-Clause License.


  GNU General Public License version 3

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.


  The BSD 2-Clause License

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
  COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

*/

#include "vsearch.h"
#include <cinttypes>  // macros PRIu64 and PRId64
#include <cstdint>  // int64_t
#include <cstdio>  // std::FILE, std::fprintf
#include <vector>


// subsampling refactoring:
//  - split with and without sizein,
//  - without sizein:
//    - equivalent to a shuffle + resize() + sort()
//  - with sizein:
//    - std::discrete_distribution()


auto subsample() -> void
{
  std::FILE * fp_fastaout = nullptr;
  std::FILE * fp_fastaout_discarded = nullptr;
  std::FILE * fp_fastqout = nullptr;
  std::FILE * fp_fastqout_discarded = nullptr;

  if (opt_fastaout != nullptr)
    {
      fp_fastaout = fopen_output(opt_fastaout);
      if (fp_fastaout == nullptr)
        {
          fatal("Unable to open FASTA output file for writing");
        }
    }

  if (opt_fastaout_discarded != nullptr)
    {
      fp_fastaout_discarded = fopen_output(opt_fastaout_discarded);
      if (fp_fastaout_discarded == nullptr)
        {
          fatal("Unable to open FASTA output file for writing");
        }
    }

  if (opt_fastqout != nullptr)
    {
      fp_fastqout = fopen_output(opt_fastqout);
      if (fp_fastqout == nullptr)
        {
          fatal("Unable to open FASTQ output file for writing");
        }
    }

  if (opt_fastqout_discarded != nullptr)
    {
      fp_fastqout_discarded = fopen_output(opt_fastqout_discarded);
      if (fp_fastqout_discarded == nullptr)
        {
          fatal("Unable to open FASTQ output file for writing");
        }
    }

  db_read(opt_fastx_subsample, 0);
  show_rusage();

  if ((fp_fastqout != nullptr or fp_fastqout_discarded != nullptr) and not db_is_fastq())
    {
      fatal("Cannot write FASTQ output with a FASTA input file, lacking quality scores");
    }

  int const dbsequencecount = db_getsequencecount();

  // create deck:
  // - if not sizein, then { fill(1) ; return deck }
  // - if sizein, then { counter ; range-for loop get_abundance ; return deck }

  // compute mass_total = std::accumulate(begin, end, 0ULL);

  uint64_t mass_total = 0;

  if (not opt_sizein)
    {
      mass_total = dbsequencecount;
    }
  else
    {
      for(int i = 0; i < dbsequencecount; i++)
        {
          mass_total += db_getabundance(i);
        }
    }

  if (not opt_quiet)
    {
      fprintf(stderr, "Got %" PRIu64 " reads from %d amplicons\n",
              mass_total, dbsequencecount);
    }

  if (opt_log != nullptr)
    {
      fprintf(fp_log, "Got %" PRIu64 " reads from %d amplicons\n",
              mass_total, dbsequencecount);
    }


  std::vector<int> abundance(dbsequencecount);
  // refactoring: default abundance values should be 1?

  uint64_t n = 0;                              /* number of reads to sample */
  if (opt_sample_size)
    {
      n = opt_sample_size;
    }
  else
    {
      n = mass_total * opt_sample_pct / 100.0;
    }

  if (n > mass_total)
    {
      fatal("Cannot subsample more reads than in the original sample");
    }

  uint64_t x = n;                          /* number of reads left */
  int a = 0;                                    /* amplicon number */
  uint64_t r = 0;                          /* read being checked */
  uint64_t m = 0;                          /* accumulated mass */

  uint64_t mass =                          /* mass of current amplicon */
    opt_sizein ? db_getabundance(0) : 1;

  // refactoring C++17: std::sample()
  progress_init("Subsampling", mass_total);
  while (x > 0)
    {
      uint64_t const random = random_ulong(mass_total - r);

      if (random < x)
        {
          /* selected read r from amplicon a */
          abundance[a]++;
          --x;
        }

      ++r;
      ++m;
      if (m >= mass)
        {
          /* next amplicon */
          ++a;
          mass = opt_sizein ? db_getabundance(a) : 1;
          m = 0;
        }
      progress_update(r);
    }
  progress_done();

  int samples = 0;
  int discarded = 0;
  progress_init("Writing output", dbsequencecount);
  for(int i = 0; i < dbsequencecount; i++)
    {
      int64_t const ab_sub = abundance[i];
      int64_t const ab_discarded = (opt_sizein ? db_getabundance(i) : 1) - ab_sub;

      if (ab_sub > 0)
        {
          ++samples;

          if (opt_fastaout != nullptr)
            {
              fasta_print_general(fp_fastaout,
                                  nullptr,
                                  db_getsequence(i),
                                  db_getsequencelen(i),
                                  db_getheader(i),
                                  db_getheaderlen(i),
                                  ab_sub,
                                  samples,
                                  -1.0,
                                  -1, -1, nullptr, 0.0);
            }

          if (opt_fastqout != nullptr)
            {
              fastq_print_general(fp_fastqout,
                                  db_getsequence(i),
                                  db_getsequencelen(i),
                                  db_getheader(i),
                                  db_getheaderlen(i),
                                  db_getquality(i),
                                  ab_sub,
                                  samples,
                                  -1.0);
            }
        }

      if (ab_discarded > 0)
        {
          ++discarded;

          if (opt_fastaout_discarded != nullptr)
            {
              fasta_print_general(fp_fastaout_discarded,
                                  nullptr,
                                  db_getsequence(i),
                                  db_getsequencelen(i),
                                  db_getheader(i),
                                  db_getheaderlen(i),
                                  ab_discarded,
                                  discarded,
                                  -1.0,
                                  -1, -1, nullptr, 0.0);
            }

          if (opt_fastqout_discarded != nullptr)
            {
              fastq_print_general(fp_fastqout_discarded,
                                  db_getsequence(i),
                                  db_getsequencelen(i),
                                  db_getheader(i),
                                  db_getheaderlen(i),
                                  db_getquality(i),
                                  ab_discarded,
                                  discarded,
                                  -1.0);
            }
        }
      progress_update(i);
    }
  progress_done();


  if (not opt_quiet)
    {
      fprintf(stderr, "Subsampled %" PRIu64 " reads from %d amplicons\n", n, samples);
    }
  if (opt_log != nullptr)
    {
      fprintf(fp_log, "Subsampled %" PRIu64 " reads from %d amplicons\n", n, samples);
    }

  db_free();

  if (opt_fastaout != nullptr)
    {
      fclose(fp_fastaout);
    }

  if (opt_fastqout != nullptr)
    {
      fclose(fp_fastqout);
    }

  if (opt_fastaout_discarded != nullptr)
    {
      fclose(fp_fastaout_discarded);
    }

  if (opt_fastqout_discarded != nullptr)
    {
      fclose(fp_fastqout_discarded);
    }
}
