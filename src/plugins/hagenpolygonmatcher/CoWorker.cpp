#include "CoWorker.h"
#include <cmath>

CoWorker::CoWorker(QSemaphore *semaphore, volatile bool *aborted) :
	semaphore(semaphore),
	aborted(aborted)
{
	moveToThread(&thread);
	connect(this, SIGNAL(computesqrts(SRing2 *, FuncsArg *)), this, SLOT(computesqrtsslot(SRing2 *, FuncsArg *)));
	connect(this, SIGNAL(computeatans(SRing2 *, FuncsArg *)), this, SLOT(computeatansslot(SRing2 *, FuncsArg *)));
	connect(this, SIGNAL(initlookupinvforbase(int, SRing2 *, SRing2 *, LookupArg *, double)), this, SLOT(initlookupinvforbaseslot(int, SRing2 *, SRing2 *, LookupArg *, double)));
	connect(this, SIGNAL(initshortcutsforbase(int, SRing2 *, SRing2 *, LookupArg *)), this, SLOT(initshortcutsforbaseslot(int, SRing2 *, SRing2 *, LookupArg *)));
	connect(this, SIGNAL(initshortcutsformatch(int, SRing2 *, SRing2 *, LookupArg *)), this, SLOT(initshortcutsformatchslot(int, SRing2 *, SRing2 *, LookupArg *)));
	connect(this, SIGNAL(computeshortcutsinv(int, int, SRing2 *, SRing2 *, LookupArg *)), this, SLOT(computeshortcutsinvslot(int, int, SRing2 *, SRing2 *, LookupArg *)));
	connect(this, SIGNAL(updateexitcosts(int , SRing2 *, SRing2 *, LookupArg *)), this, SLOT(updateexitcostsslot(int , SRing2 *, SRing2 *, LookupArg *)));
	connect(this, SIGNAL(matchinv(/**/int , int , SRing2 *, SRing2 *, FuncsArg *, FuncsArg *, LookupArg *)), this, SLOT(matchinvslot(/**/int , int , SRing2 *, SRing2 *, FuncsArg *, FuncsArg *, LookupArg *)));
	connect(this, SIGNAL(findbestgate(int, int, SRing2 *, SRing2 *, ConstraintArg*, LookupArg *, Matching**)), this, SLOT(findbestgateslot(int, int, SRing2 *, SRing2 *, ConstraintArg*, LookupArg *, Matching**)));
	thread.start();
}

CoWorker::~CoWorker()
{
	thread.exit();
	thread.wait();
}

void CoWorker::computesqrtsslot(SRing2 *base, FuncsArg *sqrts)
{
	for (int i = 0; i < base->ring.n; i++)
	{
		sqrts[i][i] = 0.;
		Point &p1 = base->ring.ring[i];
		for (int j = i + 1; j < base->ring.n; j++)
		{
			Point &p2 = base->ring.ring[j];
			double dx = p1.x - p2.x, dy = p1.y - p2.y;
			sqrts[i][j] = sqrt(dx * dx + dy * dy);
			sqrts[j][i] = sqrts[i][j];
		}
	}
	semaphore->release();
}

void CoWorker::computeatansslot(SRing2 *base, FuncsArg *atans)
{
	for (int i = 0; i < base->ring.n; i++)
	{
		atans[i][i] = 0.;
		Point &p1 = base->ring.ring[i];
		for (int j = i + 1; j < base->ring.n; j++)
		{
			Point &p2 = base->ring.ring[j];
			double dx = p2.x - p1.x, dy = p2.y - p1.y;
			atans[i][j] = atan2(dx, dy);
			atans[j][i] = atans[i][j] > 0. ? atans[i][j] - H_PI : atans[i][j] + H_PI;
		}
	}
	semaphore->release();
}

/**/
void CoWorker::initlookupinvforbaseslot(int basei, SRing2 *base, SRing2 *match, LookupArg *lookup, double skiparea)
{
	if (*aborted == false)
	{
		Matching *tempmatching = new Matching[match->ring.n];
		for (int i = 0; i < match->ring.n; i++)
		{
			tempmatching[i].quality = 0.0;
			tempmatching[i].maxquality = 0.0;
			tempmatching[i].base1 = basei;
			tempmatching[i].leftback = nullptr;
			tempmatching[i].rightback = nullptr;
		}

		LookupArg &lookupbase = lookup[basei];
		lookupbase = new Lookup*[base->ring.n];
		lookupbase[basei] = nullptr;

		{
			Lookup *&lookupmatch = lookupbase[(basei + 1) % base->ring.n];
			lookupmatch = new Lookup[match->ring.n];

			for (int matchi = 0; matchi < match->ring.n; matchi++)
			{
				tempmatching[0].cost = 0.0;
				tempmatching[0].match1 = matchi;
				//tempmatching[0].match2 = matchi + 1;
				//tempmatching[0].base2 = basei + 1;
				Lookup &lookupend = lookupmatch[matchi];
				lookupend.begin = matchi + 1;

				double matchareaabs = 0.0;
				Point &matchp1 = match->ring.ring[matchi];
				for (int matchj = matchi + 1, matchk = matchi + 2; ; matchj = matchk, matchk++)
				{
					Point &matchp2 = match->ring.ring[matchj % match->ring.n], &matchp3 = match->ring.ring[matchk % match->ring.n];
					double &matchp1x = matchp1.x, &matchp1y = matchp1.y, &matchp2x = matchp2.x, &matchp2y = matchp2.y, &matchp3x = matchp3.x, &matchp3y = matchp3.y;
					double matchp1yp2y = matchp1y - matchp2y, matchp1xp3x = matchp1x - matchp3x, matchp1yp3y = matchp1y - matchp3y, matchp2xp1x = matchp2x - matchp1x;
					double matchareal = -.5 * (matchp1yp2y * matchp1xp3x + matchp1yp3y * matchp2xp1x);
					matchareaabs += abs(matchareal);
					if (matchareaabs > skiparea)
					{
						lookupend.matching = new Matching[matchk - matchi - 1];
						memcpy_s(lookupend.matching, sizeof(Matching) * (matchk - matchi - 1), tempmatching, sizeof(Matching) * (matchk - matchi - 1));
						lookupend.end = matchk - 1;
						break;
					}
					//tempmatching[matchk - matchi - 1].quality = -2.0 * matchareaabs;
					tempmatching[matchk - matchi - 1].match1 = matchi;
					//tempmatching[matchk - matchi - 1].match2 = matchk;
					//tempmatching[matchk - matchi - 1].base2 = basei + 1;
					if (matchk == matchi + match->ring.n - 1)
					{
						lookupend.matching = new Matching[matchk - matchi];
						memcpy_s(lookupend.matching, sizeof(Matching) * (matchk - matchi), tempmatching, sizeof(Matching) * (matchk - matchi));
						lookupend.end = matchk;
						break;
					}
				}




				if (*aborted)
				{
					for (matchi++; matchi < match->ring.n; matchi++)
					{
						lookupmatch[matchi].matching = nullptr;
					}
					break;
				}
			}

		}


		if (*aborted == false)
		{
			double baseareaabs = 0.0;
			Point &basep1 = base->ring.ring[basei];
			for (int basej = basei + 1, basek = basei + 2; basek < basei + base->ring.n; basej = basek++)
			{
				Point &basep2 = base->ring.ring[basej % base->ring.n], &basep3 = base->ring.ring[basek % base->ring.n];
				double &basep1x = basep1.x, &basep1y = basep1.y, &basep2x = basep2.x, &basep2y = basep2.y, &basep3x = basep3.x, &basep3y = basep3.y;
				double basep1yp2y = basep1y - basep2y, basep1xp3x = basep1x - basep3x, basep1yp3y = basep1y - basep3y, basep2xp1x = basep2x - basep1x;
				double baseareal = .5 * (basep1yp2y * basep1xp3x + basep1yp3y * basep2xp1x);
				baseareaabs += abs(baseareal);

				Lookup *&lookupmatch = lookupbase[basek % base->ring.n];
				lookupmatch = new Lookup[match->ring.n];

				for (int matchi = 0; matchi < match->ring.n; matchi++)
				{
					//tempmatching[0].quality = -2.0 * baseareaabs;
					tempmatching[0].match1 = matchi;
					//tempmatching[0].match2 = matchi + 1;
					//tempmatching[0].base2 = basek;
					int begin = baseareaabs > skiparea ? -1 : matchi + 1;
					double matchareaabs = 0.0;
					Point &matchp1 = match->ring.ring[matchi];
					for (int matchj = matchi + 1, matchk = matchi + 2; ; matchj = matchk, matchk++)
					{
						Point &matchp2 = match->ring.ring[matchj % match->ring.n], &matchp3 = match->ring.ring[matchk % match->ring.n];
						double &matchp1x = matchp1.x, &matchp1y = matchp1.y, &matchp2x = matchp2.x, &matchp2y = matchp2.y, &matchp3x = matchp3.x, &matchp3y = matchp3.y;
						double matchp1yp2y = matchp1y - matchp2y, matchp1xp3x = matchp1x - matchp3x, matchp1yp3y = matchp1y - matchp3y, matchp2xp1x = matchp2x - matchp1x;
						double matchareal = -.5 * (matchp1yp2y * matchp1xp3x + matchp1yp3y * matchp2xp1x);
						matchareaabs += abs(matchareal);
						if (begin < 0 && baseareaabs - matchareaabs < skiparea)
						{
							begin = matchk;
						}
						if (begin > -1 && (matchareaabs - baseareaabs) > skiparea)
						{
							Lookup &lookupend = lookupmatch[matchi];
							lookupend.begin = begin;


							lookupend.matching = new Matching[matchk - begin];
							memcpy_s(lookupend.matching, sizeof(Matching) * (matchk - begin), &tempmatching[begin - matchi - 1], sizeof(Matching) * (matchk - begin));
							lookupend.end = matchk - 1;
							break;
						}
						//tempmatching[matchk - matchi - 1].quality = -2.0 * matchareaabs - 2.0 * baseareaabs;
						tempmatching[matchk - matchi - 1].match1 = matchi;
						//tempmatching[matchk - matchi - 1].match2 = matchk;
						//tempmatching[matchk - matchi - 1].base2 = basek;
						if (matchk == matchi + match->ring.n - 1)
						{
							Lookup &lookupend = lookupmatch[matchi];
							if (begin < 0)
							{
								lookupend.begin = 0;
								lookupend.end = -1;
								lookupend.matching = nullptr;
							}
							else
							{
								lookupend.begin = begin;
								lookupend.matching = new Matching[matchk - begin + 1];
								memcpy_s(lookupend.matching, sizeof(Matching) * (matchk - begin + 1), &tempmatching[begin - matchi - 1], sizeof(Matching) * (matchk - begin + 1));
								lookupend.end = matchk;
							}
							break;
						}
					}



					if (*aborted)
					{
						for (matchi++; matchi < match->ring.n; matchi++)
						{
							lookupmatch[matchi].matching = nullptr;
						}
						break;
					}

				}



				if (*aborted)
				{
					for (basek++; basek < basei + base->ring.n; basek++)
					{
						lookupbase[basek % base->ring.n] = nullptr;
					}
				}
			}//basek

		}


		delete[] tempmatching;
	}


	semaphore->release();
}


/*
void CoWorker::initlookupinvforbaseslot(int basei, SRing2 *base, SRing2 *match, LookupArg *lookup, double skiparea)
{
	if (*aborted == false)
	{
		Matching *tempmatching = new Matching[match->ring.n];
		for (int i = 0; i < match->ring.n; i++)
		{
			tempmatching[i].base1 = basei;
			tempmatching[i].leftback = nullptr;
			tempmatching[i].rightback = nullptr;
			tempmatching[i].exitcost = -DBL_MAX;
		}

		LookupArg lookupbase = lookup[basei];

		{
			Lookup *lookupmatch = lookupbase[0];

			Point &basep1 = base->ring.ring[basei], &basep2 = base->ring.ring[basei + 1];
			double basedx = basep2.x - basep1.x, basedy = basep2.y - basep1.y;
			double basesq = basedx * basedx + basedy * basedy;

			for (int matchi = 0; matchi < match->ring.n; matchi++)
			{
				tempmatching[0].quality = 0.0;
				tempmatching[0].match1 = matchi;
				tempmatching[0].match2 = matchi + 1;
				tempmatching[0].base2 = basei + 1;

				Lookup &lookupend = lookupmatch[matchi];
				lookupend.begin = matchi + 1;

				double matchareaabs = 0.0;
				Point &matchp1 = match->ring.ring[matchi];
				for (int matchj = matchi + 1, matchk = matchi + 2; ; matchj = matchk, matchk++)
				{
					Point &matchp2 = match->ring.ring[matchj % match->ring.n], &matchp3 = match->ring.ring[matchk % match->ring.n];
					double &matchp1x = matchp1.x, &matchp1y = matchp1.y, &matchp2x = matchp2.x, &matchp2y = matchp2.y, &matchp3x = matchp3.x, &matchp3y = matchp3.y;
					double matchp1yp2y = matchp1y - matchp2y, matchp1xp3x = matchp1x - matchp3x, matchp1yp3y = matchp1y - matchp3y, matchp2xp1x = matchp2x - matchp1x;
					double matchareal = -.5 * (matchp1yp2y * matchp1xp3x + matchp1yp3y * matchp2xp1x);
					matchareaabs += abs(matchareal);
					if (matchareaabs > skiparea)
					{
						lookupend.matching = new Matching[matchk - matchi - 1];
						memcpy_s(lookupend.matching, sizeof(Matching) * (matchk - matchi - 1), tempmatching, sizeof(Matching) * (matchk - matchi - 1));
						lookupend.end = matchk - 1;
						break;
					}
					double matchdx = matchp3.x - matchp1.x, matchdy = matchp3.y - matchp1.y;
					double matchsq = matchdx * matchdx + matchdy * matchdy;
					double &quality = tempmatching[matchk - matchi - 1].quality;
					quality = 0.0;
					for (int matchpeak = matchi + 1; matchpeak < matchk; matchpeak++)
					{
						Point &matchpeakp = match->ring.ring[matchpeak % match->ring.n];
						double matchleftdx = matchpeakp.x - matchp1.x, matchleftdy = matchpeakp.y - matchp1.y;
						double matchleftsq = matchleftdx * matchleftdx + matchleftdy * matchleftdy;
						double matchrightdx = matchp3.x - matchpeakp.x, matchrightdy = matchp3.y - matchpeakp.y;
						double matchrightsq = matchrightdx * matchrightdx + matchrightdy * matchrightdy;
						double cost = -2.0 * (matchleftsq + matchrightsq);
						if (cost < quality) quality = cost;
					}
					quality -= matchsq > basesq ? basesq - matchsq : matchsq - basesq;
					tempmatching[matchk - matchi - 1].match1 = matchi;
					tempmatching[matchk - matchi - 1].match2 = matchk;
					tempmatching[matchk - matchi - 1].base2 = basei + 1;
					if (matchk == matchi + match->ring.n - 1)
					{
						lookupend.matching = new Matching[matchk - matchi];
						memcpy_s(lookupend.matching, sizeof(Matching) * (matchk - matchi), tempmatching, sizeof(Matching) * (matchk - matchi));
						lookupend.end = matchk;
						break;
					}
				}


				if (*aborted)
				{
					for (matchi++; matchi < match->ring.n; matchi++)
					{
						lookupmatch[matchi].matching = nullptr;
					}
					break;
				}
			}
		}

		if (*aborted == false)
		{
			double baseareaabs = 0.0;
			Point &basep1 = base->ring.ring[basei];

			for (int basej = basei + 1, basek = basei + 2; basek < base->ring.n; basej = basek++)
			{
				Point &basep2 = base->ring.ring[basej], &basep3 = base->ring.ring[basek];
				double &basep1x = basep1.x, &basep1y = basep1.y, &basep2x = basep2.x, &basep2y = basep2.y, &basep3x = basep3.x, &basep3y = basep3.y;
				double basep1yp2y = basep1y - basep2y, basep1xp3x = basep1x - basep3x, basep1yp3y = basep1y - basep3y, basep2xp1x = basep2x - basep1x;
				double baseareal = .5 * (basep1yp2y * basep1xp3x + basep1yp3y * basep2xp1x);
				baseareaabs += abs(baseareal);
				double basedx = basep3.x - basep1.x, basedy = basep3.y - basep1.y;
				double basesq = basedx * basedx + basedy * basedy;

				Lookup *lookupmatch = lookupbase[basek - basei - 1];

				for (int matchi = 0; matchi < match->ring.n; matchi++)
				{
					Point &matchp1 = match->ring.ring[matchi], &matchendp = match->ring.ring[(matchi + 1) % match->ring.n];
					double matchdx = matchendp.x - matchp1.x, matchdy = matchendp.y - matchp1.y;
					double matchsq = matchdx * matchdx + matchdy * matchdy;
					double &quality = tempmatching[0].quality;
					quality = 0.0;
					for (int basepeak = basei + 1; basepeak < basek; basepeak++)
					{
						Point &basepeakp = base->ring.ring[basepeak % base->ring.n];
						double baseleftdx = basepeakp.x - basep1.x, baseleftdy = basepeakp.y - basep1.y;
						double baseleftsq = baseleftdx * baseleftdx + baseleftdy * baseleftdy;
						double baserightdx = basep3.x - basepeakp.x, baserightdy = basep3.y - basepeakp.y;
						double baserightsq = baserightdx * baserightdx + baserightdy * baserightdy;
						double cost = -2.0 * (baseleftsq + baserightsq);
						if (cost < quality) quality = cost;
					}
					quality -= matchsq > basesq ? basesq - matchsq : matchsq - basesq;
					tempmatching[0].match1 = matchi;
					tempmatching[0].match2 = matchi + 1;
					tempmatching[0].base2 = basek;


					int begin = baseareaabs > skiparea ? -1 : matchi + 1;
					double matchareaabs = 0.0;
					for (int matchj = matchi + 1, matchk = matchi + 2; ; matchj = matchk, matchk++)
					{
						Point &matchp2 = match->ring.ring[matchj % match->ring.n], &matchp3 = match->ring.ring[matchk % match->ring.n];
						double &matchp1x = matchp1.x, &matchp1y = matchp1.y, &matchp2x = matchp2.x, &matchp2y = matchp2.y, &matchp3x = matchp3.x, &matchp3y = matchp3.y;
						double matchp1yp2y = matchp1y - matchp2y, matchp1xp3x = matchp1x - matchp3x, matchp1yp3y = matchp1y - matchp3y, matchp2xp1x = matchp2x - matchp1x;
						double matchareal = -.5 * (matchp1yp2y * matchp1xp3x + matchp1yp3y * matchp2xp1x);
						matchareaabs += abs(matchareal);
						if (begin < 0 && baseareaabs - matchareaabs < skiparea)
						{
							begin = matchk;
						}
						if (begin > -1 && (matchareaabs - baseareaabs) > skiparea)
						{
							Lookup &lookupend = lookupmatch[matchi];
							lookupend.begin = begin;


							lookupend.matching = new Matching[matchk - begin];
							memcpy_s(lookupend.matching, sizeof(Matching) * (matchk - begin), &tempmatching[begin - matchi - 1], sizeof(Matching) * (matchk - begin));
							lookupend.end = matchk - 1;
							break;
						}
						tempmatching[matchk - matchi - 1].quality = -DBL_MAX;
						tempmatching[matchk - matchi - 1].match1 = matchi;
						tempmatching[matchk - matchi - 1].match2 = matchk;
						tempmatching[matchk - matchi - 1].base2 = basek;
						if (matchk == matchi + match->ring.n - 1)
						{
							Lookup &lookupend = lookupmatch[matchi];
							if (begin < 0)
							{
								lookupend.begin = 0;
								lookupend.end = -1;
								lookupend.matching = nullptr;
							}
							else
							{
								lookupend.begin = begin;
								lookupend.matching = new Matching[matchk - begin + 1];
								memcpy_s(lookupend.matching, sizeof(Matching) * (matchk - begin + 1), &tempmatching[begin - matchi - 1], sizeof(Matching) * (matchk - begin + 1));
								lookupend.end = matchk;
							}
							break;
						}
					}



					if (*aborted)
					{
						for (matchi++; matchi < match->ring.n; matchi++)
						{
							lookupmatch[matchi].matching = nullptr;
						}
						break;
					}

				}



				if (*aborted)
				{
					for (basek++; basek < basei + base->ring.n; basek++)
					{
						Lookup *lookupmatch = lookupbase[basek - basei - 1];
						for (int matchi = 0; matchi < match->ring.n; matchi++)
						{
							lookupmatch[matchi].matching = nullptr;
						}
					}
				}
			}//basek

		}


		delete[] tempmatching;
	}


	semaphore->release();
}
*/

void CoWorker::computeshortcutsinvslot(int basei, int basecut, SRing2 *base, SRing2 *match, LookupArg *lookup)
{

	Point &pbasei = base->ring.ring[basei], &pbasej = base->ring.ring[(basei + basecut) % base->ring.n];



	for (int matchcut = 2; matchcut < match->ring.n; matchcut++)
	{
		for (int matchi = 0; matchi < match->ring.n; matchi++)
		{
			Lookup &lookupl = lookup[basei][(basei + basecut) % base->ring.n][matchi];
			if (lookupl.begin > matchi + matchcut || lookupl.end < matchi + matchcut) continue;

			Point &pmatchi = match->ring.ring[matchi], &pmatchj = match->ring.ring[(matchi + matchcut) % match->ring.n];

			Matching &gate = lookupl.matching[matchi + matchcut - lookupl.begin];
			double &quality = gate.cost;
			quality = -DBL_MAX;

			for (int basepeak = basei + 1; basepeak < basei + basecut; basepeak++)
			{
				Point &pbasepeak = base->ring.ring[basepeak % base->ring.n];
				double &basep1x = pbasei.x, &basep1y = pbasei.y, &basep2x = pbasepeak.x, &basep2y = pbasepeak.y, &basep3x = pbasej.x, &basep3y = pbasej.y;
				double basep1yp2y = basep1y - basep2y, basep1xp3x = basep1x - basep3x, basep1yp3y = basep1y - basep3y, basep2xp1x = basep2x - basep1x;
				double basearealabs = abs(basep1yp2y * basep1xp3x + basep1yp3y * basep2xp1x);


				Lookup &lookupleft = lookup[basei][basepeak % base->ring.n][matchi];
				int matchpeakend = std::min(lookupleft.end, matchi + matchcut - 1);
				for (int matchpeak = lookupleft.begin; matchpeak <= matchpeakend; matchpeak++)
				{
					Lookup &lookupright = lookup[basepeak % base->ring.n][(basei + basecut) % base->ring.n][matchpeak % match->ring.n];
					int matchindexright = matchpeak >= match->ring.n ? matchi + matchcut - match->ring.n : matchi + matchcut;
					if (matchindexright < lookupright.begin || matchindexright > lookupright.end) continue;


					Matching &right = lookupright.matching[matchindexright - lookupright.begin];
					Matching &left = lookupleft.matching[matchpeak - lookupleft.begin];

					Point &pmatchpeak = match->ring.ring[matchpeak % match->ring.n];
					double &matchp1x = pmatchi.x, &matchp1y = pmatchi.y, &matchp2x = pmatchpeak.x, &matchp2y = pmatchpeak.y, &matchp3x = pmatchj.x, &matchp3y = pmatchj.y;
					double matchp1yp2y = matchp1y - matchp2y, matchp1xp3x = matchp1x - matchp3x, matchp1yp3y = matchp1y - matchp3y, matchp2xp1x = matchp2x - matchp1x;
					double matchareal = matchp1yp2y * matchp1xp3x + matchp1yp3y * matchp2xp1x;


					double dynq = left.cost + right.cost - abs(matchareal) - basearealabs;
					if (dynq > quality)
					{
						quality = dynq;
					}

					if (*aborted)
					{
						break;
					}
				}//matchpeak


				if (*aborted)
				{
					break;
				}
			}//basepeak

			if (*aborted)
			{
				break;
			}
		}//matchi

		if (*aborted)
		{
			break;
		}
	}//matchcut


	semaphore->release();
}

void CoWorker::initshortcutsforbaseslot(int basei, SRing2 *base, SRing2 *match, LookupArg *lookup)
{
	if (*aborted == false)
	{
		Lookup *lookupbase = lookup[basei][(basei + 1) % base->ring.n];

		for (int matchcut = 2; matchcut < match->ring.n; matchcut++)
		{
			for (int matchi = 0; matchi < match->ring.n; matchi++)
			{
				Lookup &lookupmatch = lookupbase[matchi];

				int matchj = matchi + matchcut;
				if (matchj > lookupmatch.end) continue;

				Point &matchp1 = match->ring.ring[matchi], &matchp3 = match->ring.ring[matchj % match->ring.n];

				Matching &gate = lookupmatch.matching[matchj - lookupmatch.begin];
				double &quality = gate.cost;
				quality = -DBL_MAX;

				for (int matchpeak = matchi + 1; matchpeak < matchj; matchpeak++)
				{
					Lookup &lookupmatchright = lookupbase[matchpeak % match->ring.n];
					int iright;
					if (matchpeak < match->ring.n)
					{
						if (matchj < lookupmatchright.begin || matchj > lookupmatchright.end) continue;
						iright = matchj;
					}
					else
					{
						int matchjdown = matchj - match->ring.n;
						if (matchjdown < lookupmatchright.begin || matchjdown > lookupmatchright.end) continue;
						iright = matchjdown;
					}

					Matching &right = lookupmatchright.matching[iright - lookupmatchright.begin];
					Matching &left = lookupmatch.matching[matchpeak - lookupmatch.begin];

					Point &matchp2 = match->ring.ring[matchpeak % match->ring.n];
					double &matchp1x = matchp1.x, &matchp1y = matchp1.y, &matchp2x = matchp2.x, &matchp2y = matchp2.y, &matchp3x = matchp3.x, &matchp3y = matchp3.y;
					double matchp1yp2y = matchp1y - matchp2y, matchp1xp3x = matchp1x - matchp3x, matchp1yp3y = matchp1y - matchp3y, matchp2xp1x = matchp2x - matchp1x;
					double matchareal = matchp1yp2y * matchp1xp3x + matchp1yp3y * matchp2xp1x;

					double dynq = right.cost + left.cost - abs(matchareal);
					if (dynq > quality) quality = dynq;
				}


				if (*aborted)
				{
					break;
				}
			}//matchi

			if (*aborted)
			{
				break;
			}
		}//matchcut

	}

	semaphore->release();
}

void CoWorker::initshortcutsformatchslot(int matchi, SRing2 *base, SRing2 *match, LookupArg *lookup)
{
	if (*aborted == false)
	{
		int matchj = matchi + 1;
		for (int basecut = 2; basecut < base->ring.n; basecut++)
		{
			for (int basei = 0; basei < base->ring.n; basei++)
			{
				Lookup &lookupmatch = lookup[basei][(basei + basecut) % base->ring.n][matchi];
				if (matchj < lookupmatch.begin || matchj > lookupmatch.end) continue;

				Point &basep1 = base->ring.ring[basei], &basep3 = base->ring.ring[(basei + basecut) % base->ring.n];

				Matching &gate = lookupmatch.matching[matchj - lookupmatch.begin];
				double &quality = gate.cost;
				quality = -DBL_MAX;

				for (int basepeak = basei + 1; basepeak < basei + basecut; basepeak++)
				{
					Lookup &lookupleft = lookup[basei][basepeak % base->ring.n][matchi];
					if (lookupleft.begin > matchj || lookupleft.end < matchj) continue;

					Lookup &lookupright = lookup[basepeak % base->ring.n][(basei + basecut) % base->ring.n][matchi];
					if (lookupright.begin > matchj || lookupleft.end < matchj) continue;

					Matching &left = lookupleft.matching[matchj - lookupleft.begin];
					Matching &right = lookupright.matching[matchj - lookupright.begin];

					Point &basep2 = base->ring.ring[basepeak % base->ring.n];
					double &basep1x = basep1.x, &basep1y = basep1.y, &basep2x = basep2.x, &basep2y = basep2.y, &basep3x = basep3.x, &basep3y = basep3.y;
					double basep1yp2y = basep1y - basep2y, basep1xp3x = basep1x - basep3x, basep1yp3y = basep1y - basep3y, basep2xp1x = basep2x - basep1x;
					double baseareal = basep1yp2y * basep1xp3x + basep1yp3y * basep2xp1x;

					double dynq = left.cost + right.cost - abs(baseareal);
					if (dynq > quality) quality = dynq;
				}

				if (*aborted)
				{
					break;
				}
			}//basei

			if (*aborted)
			{
				break;
			}
		}//basecut
	}

	semaphore->release();
}

void CoWorker::updateexitcostsslot(int basei, SRing2 *base, SRing2 *match, LookupArg *lookup)
{
	if (*aborted == false)
	{
		Point &pbasei = base->ring.ring[basei];
		LookupArg &lookup1 = lookup[basei];


		{
			int basej = basei + 1;
			Point &pbasej = base->ring.ring[basej];
			double basedx = pbasej.x - pbasei.x, basedy = pbasej.y - pbasei.y;
			double basesq = basedx * basedx + basedy * basedy;

			Lookup *lookup2 = lookup1[0];

			for (int matchi = 0; matchi < match->ring.n; matchi++)
			{
				Lookup &lookup3 = lookup2[matchi];
				Point &pmatchi = match->ring.ring[matchi];

				for (int matchj = lookup3.begin; matchj <= lookup3.end; matchj++)
				{

					Point &pmatchj = match->ring.ring[matchj % match->ring.n];
					double matchdx = pmatchj.x - pmatchi.x, matchdy = pmatchj.y - pmatchi.y;
					double matchsq = matchdx * matchdx + matchdy * matchdy;

					Matching &matching = lookup3.matching[matchj - lookup3.begin];
					//double &exitcost = matching.exitcost;
					//exitcost = 0.0;

					for (int matchpeak = matchi + 1; matchpeak < matchj; matchpeak++)
					{
						Point &pmatchpeak = match->ring.ring[matchpeak % match->ring.n];
						double matchleftdx = pmatchpeak.x - pmatchi.x, matchleftdy = pmatchpeak.y - pmatchi.y;
						double matchleftsq = matchleftdx * matchleftdx + matchleftdy * matchleftdy;
						double matchrightdx = pmatchj.x - pmatchpeak.x, matchrightdy = pmatchj.y - pmatchpeak.y;
						double matchrightsq = matchrightdx * matchrightdx + matchrightdy * matchrightdy;
						double cost = -2.0 * (matchleftsq + matchrightsq);
						//if (cost < exitcost) exitcost = cost;
					}

					//exitcost += basesq < matchsq ? basesq - matchsq : matchsq - basesq;


					if (*aborted)
					{
						break;
					}
				}//matchj


				if (*aborted)
				{
					break;
				}
			}//matchi
		}

		if (*aborted == false)
		{
			for (int basej = basei + 2; basej < base->ring.n; basej++)
			{
				Point &pbasej = base->ring.ring[basej];
				double basedx = pbasej.x - pbasei.x, basedy = pbasej.y - pbasei.y;
				double basesq = basedx * basedx + basedy * basedy;

				Lookup *lookup2 = lookup1[basej - basei - 1];

				for (int matchi = 0; matchi < match->ring.n; matchi++)
				{
					Lookup &lookup3 = lookup2[matchi];
					int matchj = matchi + match->ring.n - 1;
					if (matchj  < lookup3.begin || matchj > lookup3.end) continue;

					Point &pmatchi = match->ring.ring[matchi], &pmatchj = match->ring.ring[matchj % match->ring.n];
					double matchdx = pmatchj.x - pmatchi.x, matchdy = pmatchj.y - pmatchi.y;
					double matchsq = matchdx * matchdx + matchdy * matchdy;

					Matching &matching = lookup3.matching[matchj - lookup3.begin];
					//double &exitcost = matching.exitcost;
					//exitcost = 0.0;

					for (int basepeak = basej + 1; basepeak < base->ring.n + basei - 1; basepeak++)
					{
						Point &pbasepeak = base->ring.ring[basepeak % base->ring.n];
						double baseleftdx = pbasepeak.x - pbasej.x, baseleftdy = pbasepeak.y - pbasej.y;
						double baseleftsq = baseleftdx * baseleftdx + baseleftdy * baseleftdy;
						double baserightdx = pbasei.y - pbasepeak.x, baserightdy = pbasei.y - pbasepeak.y;
						double baserightsq = baserightdx * baserightdx + baserightdy * baserightdy;
						double basecost = -2.0 * (baseleftsq + baserightsq);
						//if (basecost < exitcost) exitcost = basecost;

					}//basepeak

					//exitcost += basesq < matchsq ? basesq - matchsq : matchsq - basesq;


					if (*aborted)
					{
						break;
					}
				}//matchi




				if (*aborted)
				{
					break;
				}
			}//basej
		}
	}
	semaphore->release();
}

//min: .333333333333
#define RELUCTANCE		.2

//min: 6.
#define STAGNATION		7.

#define SLOPE			1.

inline Matching* CoWorkerleftedge(Matching *right)
{
	if (right->leftback != nullptr)
	{
		for (Matching *rightleftback = right->leftback; ; right = rightleftback, rightleftback = rightleftback->leftback)
		{
			if (rightleftback->leftback == nullptr)
			{
				return right->rightback;
			}
		}
	}
	return nullptr;
}

inline Matching* CoWorkerrightedge(Matching *left)
{
	for (Matching *leftedge = left; ; leftedge = leftedge->rightback)
	{
		if (leftedge->rightback == nullptr)
		{
			return leftedge;
		}
	}
	return nullptr;
}

void CoWorker::matchinvslot(/**/int basei, int basecut, SRing2 *base, SRing2 *match, FuncsArg *sqrts, FuncsArg *atans, LookupArg *lookup)
{

	double baselength = sqrts[basei][(basei + basecut) % base->ring.n];

	if (baselength >= 1e-13)
	{
		Point &pbasei = base->ring.ring[basei], &pbasej = base->ring.ring[(basei + basecut) % base->ring.n];
		double basedx = pbasej.x - pbasei.x, basedy = pbasej.y - pbasei.y;
		double basedxn = basedx / baselength, basedyn = basedy / baselength;
		for (int basepeak = basei + 1; basepeak < basei + basecut; basepeak++)
		{
			Point &pbasepeak = base->ring.ring[basepeak % base->ring.n];
			double basep11yp21yleft = pbasei.y - pbasepeak.y, basep21xp11xleft = pbasepeak.x - pbasei.x;
			double baseleft = (basep21xp11xleft * basedxn - basep11yp21yleft * basedyn);
			double basep11yp21yright = pbasej.y - pbasepeak.y, basep21xp11xright = pbasepeak.x - pbasej.x;
			double baseright = (basep21xp11xright * basedxn - basep11yp21yright * basedyn);
			double baseh = -(basep11yp21yleft * basedxn + basep21xp11xleft * basedyn);
			double basehabs = abs(baseh);
			if (basehabs < 1e-13) continue;

			for (int matchcut = 2; matchcut < match->ring.n; matchcut++)
			{
				for (int matchi = 0; matchi < match->ring.n; matchi++)
				{
					double matchlength = sqrts[match->ring.n - matchi - 1][match->ring.n - ((matchi + matchcut) % match->ring.n) - 1];
					if (matchlength >= 1e-13)
					{
						Lookup &lookupl = lookup[basei][(basei + basecut) % base->ring.n][matchi];
						if (lookupl.begin > matchi + matchcut || lookupl.end < matchi + matchcut) continue;
						Lookup &lookupleft = lookup[basei][basepeak % base->ring.n][matchi];

						Point &pmatchi = match->ring.ring[matchi], &pmatchj = match->ring.ring[(matchi + matchcut) % match->ring.n];
						double matchdx = pmatchj.x - pmatchi.x, matchdy = pmatchj.y - pmatchi.y;
						double matchdxn = matchdx / matchlength, matchdyn = matchdy / matchlength;

						Matching &gate = lookupl.matching[matchi + matchcut - lookupl.begin];

						int matchpeakend = std::min(lookupleft.end, matchi + matchcut - 1);
						for (int matchpeak = lookupleft.begin; matchpeak <= matchpeakend; matchpeak++)
						{
							Lookup &lookupright = lookup[basepeak % base->ring.n][(basei + basecut) % base->ring.n][matchpeak % match->ring.n];
							int matchindexright = matchpeak >= match->ring.n ? matchi + matchcut - match->ring.n : matchi + matchcut;
							if (matchindexright < lookupright.begin || matchindexright > lookupright.end) continue;

							Matching &right = lookupright.matching[matchindexright - lookupright.begin];
							Matching &left = lookupleft.matching[matchpeak - lookupleft.begin];

							if (basepeak % base->ring.n == base->ring.n - (matchpeak % match->ring.n) - 1)
							{
								{
									Matching *rightedge = CoWorkerleftedge(&right);
									if (rightedge == nullptr)
									{
										double atan2basewing = atans[basepeak % base->ring.n][(basei + basecut) % base->ring.n];
										double atan2matchwing = atans[basepeak % base->ring.n][match->ring.n - ((matchi + matchcut) % match->ring.n) - 1];
										double delta = abs(atan2basewing - atan2matchwing);
										if (delta < MINANGLE || delta > H_2_PI - MINANGLE) 
											continue;
									}
									else
									{
										double atan2basewing = atans[basepeak % base->ring.n][rightedge->base1];
										double atan2matchwing = atans[basepeak % base->ring.n][rightedge->match1];
										double delta = abs(atan2basewing - atan2matchwing);
										if (delta < MINANGLE || delta > H_2_PI - MINANGLE) 
											continue;
									}
								}
								{
									Matching *leftedge = CoWorkerrightedge(&left);
									double atan2basewing = atans[basepeak % base->ring.n][leftedge->base1];
									double atan2matchwing = atans[basepeak % base->ring.n][leftedge->match1];
									double delta = abs(atan2basewing - atan2matchwing);
									if (delta < MINANGLE || delta > H_2_PI - MINANGLE) 
										continue;
								}
							}

							Point &pmatchpeak = match->ring.ring[matchpeak % match->ring.n];
							double matchp11yp21yleft = pmatchi.y - pmatchpeak.y, matchp21xp11xleft = pmatchpeak.x - pmatchi.x;
							double matchleft = (matchp21xp11xleft * matchdxn - matchp11yp21yleft * matchdyn);
							double matchp11yp21yright = pmatchj.y - pmatchpeak.y, matchp21xp11xright = pmatchpeak.x - pmatchj.x;
							double matchright = (matchp21xp11xright * matchdxn - matchp11yp21yright * matchdyn);

							double rightmax, rightmin;
							if (baseright < matchright)
							{
								rightmax = matchright;
								rightmin = baseright;
							}
							else
							{
								rightmin = matchright;
								rightmax = baseright;
							}
							double leftmax, leftmin;
							if (baseleft < matchleft)
							{
								leftmax = matchleft;
								leftmin = baseleft;
							}
							else
							{
								leftmin = matchleft;
								leftmax = baseleft;
							}
							double hormin = leftmin - rightmax;
							if (hormin < 1e-13) continue;

							double matchh = (matchp11yp21yleft * matchdxn + matchp21xp11xleft * matchdyn);
							double matchhabs = abs(matchh);
							if (matchhabs < 1e-13) continue;

							double dh1 = matchh / baseh;
							if (dh1 < 0.0) continue;

							double hormax = leftmax - rightmin;
							double vertmax, vertmin;
							if (basehabs > matchhabs)
							{
								vertmax = basehabs;
								vertmin = matchhabs;
							}
							else
							{
								vertmin = basehabs;
								vertmax = matchhabs;
							}

							double quality = (vertmin + (RELUCTANCE * vertmax)) / (1.0 + RELUCTANCE) * (hormin + (RELUCTANCE * hormax)) / (1.0 + RELUCTANCE);
							//double quality = vertmin * hormin * 2.;

							double maxq = std::max(left.maxquality, right.maxquality);
							if (quality < maxq * SLOPE) continue;

							double inq = left.quality + right.quality;
							//if (inq / quality > STAGNATION) continue;

							//double cost = std::max(baselength * basehabs, matchlength * matchhabs);
							double cost = vertmax * hormax;
							double incost = left.cost + right.cost;

							double dynq = (quality - cost + inq + incost);
							if (dynq > gate.quality + gate.cost)
							{
								gate.quality = quality + inq;
								gate.cost = incost - cost;
								gate.maxquality = std::max(maxq, quality);
								gate.leftback = &left;
								gate.rightback = &right;
							}

							if (*aborted)
							{
								break;
							}
						}//matchpeak

					}


					if (*aborted)
					{
						break;
					}
				}//matchi

				if (*aborted)
				{
					break;
				}
			}//matchcut


			if (*aborted)
			{
				break;
			}
		}//basepeak

	}


	semaphore->release();
}

/*
void CoWorker::matchinvslot(int basei, int basecut, SRing2 *base, SRing2 *match, LookupArg *lookup)
{

	Point &pbasei = base->ring.ring[basei], &pbasej = base->ring.ring[(basei + basecut)];
	double basedx = pbasej.x - pbasei.x, basedy = pbasej.y - pbasei.y;
	double basesq = basedx * basedx + basedy * basedy;

	for (int basepeak = basei + 1; basepeak < basei + basecut; basepeak++)
	{
		Point &pbasepeak = base->ring.ring[basepeak];
		double baseleftdx = pbasepeak.x - pbasei.x, baseleftdy = pbasepeak.y - pbasei.y;
		double baseleftsq = baseleftdx * baseleftdx + baseleftdy * baseleftdy;
		double baserightdx = pbasej.x - pbasepeak.x, baserightdy = pbasej.y - pbasepeak.y;
		double baserightsq = baserightdx * baserightdx + baserightdy * baserightdy;


		double &basep1x = pbasei.x, &basep1y = pbasei.y, &basep2x = pbasepeak.x, &basep2y = pbasepeak.y, &basep3x = pbasej.x, &basep3y = pbasej.y;
		double basep1yp2y = basep1y - basep2y, basep1xp3x = basep1x - basep3x, basep1yp3y = basep1y - basep3y, basep2xp1x = basep2x - basep1x;
		double baseareal = basep1yp2y * basep1xp3x + basep1yp3y * basep2xp1x;
		bool basesign = signbit(baseareal);


		for (int matchcut = 2; matchcut < match->ring.n; matchcut++)
		{
			for (int matchi = 0; matchi < match->ring.n; matchi++)
			{
				Lookup &lookupl = lookup[basei][basecut - 1][matchi];
				if (lookupl.begin > matchi + matchcut || lookupl.end < matchi + matchcut) continue;
				Lookup &lookupleft = lookup[basei][basepeak - basei - 1][matchi];

				Point &pmatchi = match->ring.ring[matchi], &pmatchj = match->ring.ring[(matchi + matchcut) % match->ring.n];
				double matchdx = pmatchj.x - pmatchi.x, matchdy = pmatchj.y - pmatchi.y;
				double matchsq = matchdx * matchdx + matchdy * matchdy;

				Matching &gate = lookupl.matching[matchi + matchcut - lookupl.begin];

				int matchpeakend = std::min(lookupleft.end, matchi + matchcut - 1);
				for (int matchpeak = lookupleft.begin; matchpeak <= matchpeakend; matchpeak++)
				{
					Lookup &lookupright = lookup[basepeak][basei + basecut - basepeak - 1][matchpeak % match->ring.n];
					int matchindexright = matchpeak >= match->ring.n ? matchi + matchcut - match->ring.n : matchi + matchcut;
					if (matchindexright < lookupright.begin || matchindexright > lookupright.end) continue;

					Point &pmatchpeak = match->ring.ring[matchpeak % match->ring.n];

					double &matchp1x = pmatchi.x, &matchp1y = pmatchi.y, &matchp2x = pmatchpeak.x, &matchp2y = pmatchpeak.y, &matchp3x = pmatchj.x, &matchp3y = pmatchj.y;
					double matchp1yp2y = matchp1y - matchp2y, matchp1xp3x = matchp1x - matchp3x, matchp1yp3y = matchp1y - matchp3y, matchp2xp1x = matchp2x - matchp1x;
					double matchareal = matchp1yp2y * matchp1xp3x + matchp1yp3y * matchp2xp1x;
					bool matchsign = signbit(matchareal);


					Matching &right = lookupright.matching[matchindexright - lookupright.begin];
					Matching &left = lookupleft.matching[matchpeak - lookupleft.begin];

					double matchleftdx = pmatchpeak.x - pmatchi.x, matchleftdy = pmatchpeak.y - pmatchi.y;
					double matchleftsq = matchleftdx * matchleftdx + matchleftdy * matchleftdy;

					double matchrightdx = pmatchj.x - pmatchpeak.x, matchrightdy = pmatchj.y - pmatchpeak.y;
					double matchrightsq = matchrightdx * matchrightdx + matchrightdy * matchrightdy;

					double quality;
					if (basesign == matchsign)
					{
						quality = -basesq - matchsq;
						quality -= baseleftsq + matchleftsq;
						quality -= baserightsq + matchrightsq;
					}
					else
					{
						quality = basesq < matchsq ? basesq - matchsq : matchsq - basesq;
						quality += baseleftsq < matchleftsq ? baseleftsq - matchleftsq : matchleftsq - baseleftsq;
						quality += baserightsq < matchrightsq ? baserightsq - matchrightsq : matchrightsq - baserightsq;
					}

					double dynq = quality + left.quality + right.quality;
					if (dynq > gate.quality)
					{
						gate.quality = dynq;
						gate.leftback = &left;
						gate.rightback = &right;
					}

				}//matchpeak

				if (*aborted)
				{
					break;
				}
			}//matchi

			if (*aborted)
			{
				break;
			}
		}//matchcut


		if (*aborted)
		{
			break;
		}
	}//basepeak

	semaphore->release();
}
*/

void CoWorker::findbestgateslot(int basei, int matchi, SRing2 *base, SRing2 *match, ConstraintArg *constraint, LookupArg *lookup, Matching **out)
{
	if (*aborted == false)
	{
		for (int gbasei = 0; gbasei < base->ring.n; gbasei++)
		{
			if (constraint[gbasei] > -1 && gbasei != basei)
			{
				LookupArg &lookup1 = lookup[gbasei];
				int baseiup = gbasei > basei ? basei + base->ring.n : basei;
				for (int gmatchi = 0; gmatchi < match->ring.n; gmatchi++)
				{
					if (gmatchi != matchi && constraint[gbasei] == gmatchi)
					{
						for (int gbasej = baseiup + 1; gbasej < gbasei + base->ring.n; gbasej++)
						{
							if (constraint[gbasej % base->ring.n] < 0) continue;
							Lookup *lookup2 = lookup1[gbasej % base->ring.n];
							Lookup &lookup3 = lookup2[gmatchi];
							int matchiup = matchi < gmatchi ? matchi + match->ring.n : matchi;
							int matchjbegin = std::max(matchiup + 1, lookup3.begin);
							for (int gmatchj = matchjbegin; gmatchj <= lookup3.end; gmatchj++)
							{
								if (constraint[gbasej % base->ring.n] != (gmatchj % match->ring.n)) continue;
								Matching &matching = lookup3.matching[gmatchj - lookup3.begin];
								Matching *rightback = matching.rightback;
								if (rightback == nullptr) continue;
								if (rightback->base1 == basei && rightback->match1 == matchi)
								{
									double q = matching.quality - matching.cost;
									if (*out == nullptr || q > (*out)->quality)
									{
										*out = &matching;
									}
								}
							}//gmatchj
							if (*aborted)
							{
								break;
							}
						}//gbasej
						if (*aborted)
						{
							break;
						}
					}
				}//gmatchi
				if (*aborted)
				{
					break;
				}
			}
		}//gbasei
	}
	semaphore->release();
}