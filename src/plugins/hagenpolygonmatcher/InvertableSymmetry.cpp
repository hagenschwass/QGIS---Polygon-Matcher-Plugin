#include "InvertableSymmetry.h"
#include "CoWorker.h"
#include "MircoWorker.h"

#include <QSemaphore>

inline SymmetryMatches computeSymmetryMatchesInv(SRing2 &base, SRing2 &match, FreeMatchingTree &tree)
{
	SymmetryBase sb = new BaseMatch[base.ring.n];
	SymmetryMatch sm = new PointMatch[base.ring.n];
	SymmetryMatches matches = { sb, sm, -1, -1 };

	for (int i = 0; i < base.ring.n; i++)
	{
		sm[i].base.base = -1;
		sm[i].backptr = nullptr;
		sm[i].quality = 0.0;
		//sm[i].inv = false;
		sb[i].base = i;
		sb[i].next = sb + i;
		sb[i].prev = sb + i;
	}

	sm[tree.up->base].match = base.ring.n - tree.up->match - 1;
	sm[tree.up->base].base.base = tree.up->base;
	sm[tree.up->base].gate = nullptr;
	appendPointMatch(sb[tree.up->base], sm[tree.up->base]);
	sm[tree.down->base].match = base.ring.n - tree.down->match - 1;
	sm[tree.down->base].base.base = tree.down->base;
	sm[tree.down->base].gate = nullptr;
	appendPointMatch(sb[tree.down->base], sm[tree.down->base]);

	for (int i = 0; i < tree.upcount; i++)
	{
		FreeMatching &fm = tree.up[i];
		if (fm.left == nullptr) continue;
		sm[fm.right->base].base.base = fm.right->base;
		sm[fm.right->base].match = base.ring.n - fm.right->match - 1;
		sm[fm.right->base].gate = &fm;
		appendPointMatch(sb[fm.right->base], sm[fm.right->base]);
	}

	for (int i = 0; i < tree.downcount; i++)
	{
		FreeMatching &fm = tree.down[i];
		if (fm.left == nullptr) continue;
		sm[fm.right->base].base.base = fm.right->base;
		sm[fm.right->base].match = base.ring.n - fm.right->match - 1;
		sm[fm.right->base].gate = &fm;
		appendPointMatch(sb[fm.right->base], sm[fm.right->base]);
	}

	return matches;
}

inline void findSymmetryMatchesGates(SymmetryMatches &matches, SRing2 &base, SRing2 &match, LookupT lookup, int nworkers, CoWorker** workers, QSemaphore *semaphore, volatile bool &aborted)
{
	int currentworker = 0;
	int numjobs = 0;
	for (int i = 0; i < base.ring.n; i++)
	{
		BaseMatch &basematch = matches.base[i];
		for (BaseMatch *bm = basematch.next; bm != &basematch; bm = bm->next)
		{
			PointMatch *pm = reinterpret_cast<PointMatch*>(bm);
			if (pm->gate != nullptr) continue;
			workers[currentworker]->findbestgate(i, base.ring.n - pm->match - 1, &base, &match, nullptr, lookup, nullptr);
			currentworker = (currentworker + 1) % nworkers;
			numjobs++;
		}
		if (aborted)
		{
			break;
		}
	}
	semaphore->acquire(numjobs);
}

inline bool computeSymmetryBeginEndInv(SRing2 &base, SymmetryMatches &matches)
{
	for (int ib = 0; ib < base.ring.n; ib++)
	{
		if (matches.base[ib].next != &matches.base[ib])
		{
			if (matches.match[ib].match <= ib)
			{
				bool shift = false;
				int lastmatch = ib;
				for (int ie = base.ring.n - 1; ie > ib; ie--)
				{
					if (matches.base[ie].next != &matches.base[ie])
					{
						int match = matches.match[ie].match;
						if (shift)
						{
							match += base.ring.n;
						}
						else
						{
							if (match < lastmatch)
							{
								shift = true;
								match += base.ring.n;
							}
							else
							{
								lastmatch = match;
							}
						}
						if (match >= ie)
						{
							matches.ie = ie;
							break;
						}
					}
				}
				matches.ib = ib;
				break;
			}
		}
	}
	return matches.ib > -1 && matches.ie > -1;
}

inline void orderSymmetryMatchesInv(SRing2 &base, SymmetryMatches &matches)
{
	for (int i = 0; i < matches.ib; i++)
	{
		PointMatch &match = matches.match[i];
		if (match.base.base > -1)
		{
			//leavePointMatch(match);
			BaseMatch &basematch = matches.base[match.match];
			appendPointMatch(basematch, match);
			match.base.base = match.match;
			match.match = i;
			//match.inv = true;
		}
	}
	for (int i = matches.ie + 1; i < base.ring.n; i++)
	{
		PointMatch &match = matches.match[i];
		if (match.base.base > -1)
		{
			//leavePointMatch(match);
			BaseMatch &basematch = matches.base[match.match];
			appendPointMatch(basematch, match);
			match.base.base = match.match;
			match.match = i;
			//match.inv = true;
		}
	}
}

#define MAXRELAX			7

inline PointMatch *computeBestSymmetryInv(SRing2 &base, SymmetryMatches &matches, double quality)
{
	double maxquality = -DBL_MAX;
	PointMatch *maxqualitymatch = nullptr;
	for (int i = matches.ib; i < matches.ie; i++)
	{
		int iinv = base.ring.n - i - 1;
		for (BaseMatch *bm = matches.base[i].next; bm != &matches.base[i]; bm = bm->next)
		{
			PointMatch *pm = reinterpret_cast<PointMatch*>(bm);
			int match = pm->match > i ? pm->match - base.ring.n : pm->match;

			double qualityl = pm->quality + (pm->gate == nullptr ? quality : pm->gate->quality);

			int targetsrelaxed = 0;
			for (int target = i + 1; target <= matches.ie && targetsrelaxed <= MAXRELAX; target++)
			{
				for (BaseMatch *targetbm = matches.base[target].next; targetbm != &matches.base[target]; targetbm = targetbm->next)
				{
					PointMatch *targetpm = reinterpret_cast<PointMatch*>(targetbm);
					int targetmatch = targetpm->match >= target ? targetpm->match - base.ring.n : targetpm->match;
					if (targetmatch < match)
					{
						if (qualityl > targetpm->quality)
						{
							targetpm->quality = qualityl;
							targetpm->backptr = pm;
							double exitquality = qualityl + (targetpm->gate == nullptr ? quality : targetpm->gate->quality);
							if (exitquality > maxquality)
							{
								maxquality = exitquality;
								maxqualitymatch = targetpm;
							}
						}
						targetsrelaxed++;
					}
				}
			}
		}
	}
	return maxqualitymatch;
}

inline void adjustSymmetryInv(SRing2 &match, PointMatch *pointmatch, double epsilon, int nworker, MicroWorker **microworker, QSemaphore *semaphore, volatile bool &aborted)
{
	int pairs = 0;
	for (PointMatch *runpm = pointmatch; runpm != nullptr; runpm = runpm->backptr)
	{
		pairs++;
	}
	for (int worker = 0; worker < nworker; worker++)
	{
		microworker[worker]->setupAdjustInvSymmetry((pairs / nworker) + 1, semaphore);
	}
	int currentworker = 0;

	if (aborted == false)
	{
		SRing matchout = cloneSRing(match.ring);
		for (int i = 0; i < 31; i++)
		{
			for (PointMatch *runpm = pointmatch; runpm != nullptr; runpm = runpm->backptr)
			{
				microworker[currentworker]->loadAdjustInvSymmetry(runpm);
				currentworker = (currentworker + 1) % nworker;
			}
			for (int worker = 0; worker < nworker; worker++)
			{
				microworker[worker]->runAdjustInvSymmetry(match.ring, matchout);
			}
			semaphore->acquire(pairs + nworker);
			if (aborted)
			{
				break;
			}
			swapSRings(match.ring, matchout);
		}
		deleteSRing(matchout);
	}

	for (int worker = 0; worker < nworker; worker++)
	{
		microworker[worker]->clearAdjustInvSymmetry();
	}
}

inline void updateTurnedIndexesInv(SymmetryMatches &matches)
{
	for (int i = matches.ib; i <= matches.ie; i++)
	{
		BaseMatch &base = matches.base[i];
		for (BaseMatch *bm = base.next; bm != &base; bm = bm->next)
		{
			if (bm->base != base.base)
			{
				PointMatch *pm = reinterpret_cast<PointMatch*>(bm);
				pm->match = bm->base;
				bm->base = base.base;
			}
		}
	}
}

inline SRing invertableSymmetry2Ring(SRing2 &base, SRing2 &match, PointMatch *pointmatch)
{
	int count = 0;
	for (PointMatch *run = pointmatch; run != nullptr; run = run->backptr)
	{
		count += run->base.base == run->match ? 1 : 2;
	}
	SRing result = { new Point[count], count };
	int i = 0;
	for (PointMatch *run = pointmatch; run != nullptr; run = run->backptr)
	{
		result.ring[i++] = /*run->inv ? match.ring.ring[match.ring.n - run->match - 1] : */base.ring.ring[run->match];
	}
	i = 1;
	for (PointMatch *run = pointmatch; run != nullptr; run = run->backptr)
	{
		if (run->base.base != run->match)
		{
			result.ring[count - i] = /*run->inv ? match.ring.ring[match.ring.n - run->base.base - 1] : */base.ring.ring[run->base.base];
			i++;
		}
	}
	return result;
}

InvertableSymmetry::InvertableSymmetry(SRing2 &base, SRing2 &match, MatchingResult &result, LookupT lookup) :
	Symmetry(base, match, result, lookup)
{
}

InvertableSymmetry::~InvertableSymmetry()
{
}

void filltrianglesrect(Matching *matching, int base2, int match2, SRing2 &base, std::vector<Triangle> *triangles)
{
	if (matching->rightback)
	{
		triangles->push_back({ base.ring.ring[matching->base1], base.ring.ring[base2], base.ring.ring[matching->rightback->base1] });
		filltrianglesrect(matching->rightback, base2, match2, base, triangles);
		filltrianglesrect(matching->leftback, matching->rightback->base1, matching->rightback->match1, base, triangles);
	}
}

void InvertableSymmetry::filltriangles(std::vector<Triangle> *triangles)
{
	filltrianglesrect(result.matching, result.opposite->base1, result.opposite->match1, base, triangles);
	filltrianglesrect(result.opposite, result.matching->base1, result.matching->match1, base, triangles);
}

void fillmatchtrianglesrect(Matching *matching, int base2, int match2, SRing2 &match, std::vector<Triangle> *triangles)
{
	if (matching->rightback)
	{
		triangles->push_back({ match.ring.ring[matching->match1], match.ring.ring[match2], match.ring.ring[matching->rightback->match1] });
		fillmatchtrianglesrect(matching->rightback, base2, match2, match, triangles);
		fillmatchtrianglesrect(matching->leftback, matching->rightback->base1, matching->rightback->match1, match, triangles);
	}
}

void InvertableSymmetry::fillmatchtriangles(std::vector<Triangle> *triangles)
{
	fillmatchtrianglesrect(result.matching, result.opposite->base1, result.opposite->match1, match, triangles);
	fillmatchtrianglesrect(result.opposite, result.matching->base1, result.matching->match1, match, triangles);
}