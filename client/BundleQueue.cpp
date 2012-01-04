/*
 * Copyright (C) 2011 AirDC++ Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "stdinc.h"

#include "BundleQueue.h"
#include "SettingsManager.h"
#include "AirUtil.h"
#include "QueueItem.h"
#include "LogManager.h"

namespace dcpp {


BundleQueue::BundleQueue() : 
	nextSearch(0),
	nextRecentSearch(0)
{ 
	highestSel=0, highSel=0, normalSel=0, lowSel=0, calculations=0;
}

BundleQueue::~BundleQueue() { }

void BundleQueue::add(BundlePtr aBundle) {
	addSearchPrio(aBundle, aBundle->getPriority());
	bundles[aBundle->getToken()] = aBundle;

	//check if we need to insert the root bundle dir
	if (!aBundle->getFileBundle()) {
		if (aBundle->getBundleDirs().find(aBundle->getTarget()) == aBundle->getBundleDirs().end()) {
			string releaseDir = AirUtil::getReleaseDir(aBundle->getTarget());
			if (!releaseDir.empty()) {
				bundleDirs[releaseDir] = aBundle;
			}
		}
	}
}

void BundleQueue::addSearchPrio(BundlePtr aBundle, Bundle::Priority p) {
	if (p < Bundle::LOW) {
		return;
	}

	if (aBundle->getRecent()) {
		dcassert(std::find(recentSearchQueue.begin(), recentSearchQueue.end(), aBundle) == recentSearchQueue.end());
		recentSearchQueue.push_back(aBundle);
		return;
	} else {
		dcassert(std::find(prioSearchQueue[p].begin(), prioSearchQueue[p].end(), aBundle) == prioSearchQueue[p].end());
		prioSearchQueue[p].push_back(aBundle);
	}
}

void BundleQueue::removeSearchPrio(BundlePtr aBundle, Bundle::Priority p) {
	if (p < Bundle::LOW) {
		return;
	}

	if (aBundle->getRecent()) {
		auto i = std::find(recentSearchQueue.begin(), recentSearchQueue.end(), aBundle);
		dcassert(i != recentSearchQueue.end());
		if (i != recentSearchQueue.end()) {
			recentSearchQueue.erase(i);
		}
	} else {
		auto i = std::find(prioSearchQueue[p].begin(), prioSearchQueue[p].end(), aBundle);
		dcassert(i != prioSearchQueue[p].end());
		if (i != prioSearchQueue[p].end()) {
			prioSearchQueue[p].erase(i);
		}
	}
}

void BundleQueue::setSearchPriority(BundlePtr aBundle, Bundle::Priority oldPrio, Bundle::Priority newPrio) {
	removeSearchPrio(aBundle, oldPrio);
	addSearchPrio(aBundle, newPrio);
}

BundlePtr BundleQueue::findRecent() {
	if ((int)recentSearchQueue.size() == 0) {
		return NULL;
	}
	BundlePtr tmp = recentSearchQueue.front();
	recentSearchQueue.pop_front();
	//check if the bundle still belongs to here
	if ((tmp->getDirDate() + (SETTING(RECENT_BUNDLE_HOURS)*60*60)) > (GET_TIME())) {
		//LogManager::getInstance()->message("Time remaining as recent: " + Util::toString(((tmp->getDirDate() + (SETTING(RECENT_BUNDLE_HOURS)*60*60)) - GET_TIME()) / (60)) + " minutes");
		recentSearchQueue.push_back(tmp);
	} else {
		//LogManager::getInstance()->message("REMOVE RECENT");
		tmp->setRecent(false);
		addSearchPrio(tmp, tmp->getPriority());
	}
	return tmp;
}

int BundleQueue::getPrioSum(int& prioBundles) {
	int p = Bundle::LAST - 1;
	int prioSum = 0;
	do {
		for (auto k = prioSearchQueue[p].begin(); k != prioSearchQueue[p].end(); ++k) {
			if ((*k)->countOnlineUsers() > (size_t)SETTING(AUTO_SEARCH_LIMIT)) {
				continue;
			}
			prioBundles++;
		}
		prioSum += (int)(p-1)*prioSearchQueue[p].size();
		p--;
	} while(p >= Bundle::LOW);
	return prioSum;
}

BundlePtr BundleQueue::findAutoSearch() {
	int prioBundles = 0;
	int prioSum = getPrioSum(prioBundles);

	//do we have anything where to search from?
	if (prioBundles == 0) {
		return NULL;
	}

	//choose the bundle
	BundlePtr cand = NULL;
	int rand = Util::rand(prioSum); //no results for paused or lowest
	//LogManager::getInstance()->message("prioSum: " + Util::toString(prioSum) + " random: " + Util::toString(rand));

	int p = Bundle::LOW;
	do {
		int size = (int)prioSearchQueue[p].size();
		if (rand < (int)(p-1)*size && size > 0) {
			//we got the prio
			int s = 0;
			while (s <= size) {
				BundlePtr tmp = prioSearchQueue[p].front();
				prioSearchQueue[p].pop_front();
				prioSearchQueue[p].push_back(tmp);
				if (tmp->countOnlineUsers() > (size_t)SETTING(AUTO_SEARCH_LIMIT)) {
					s++;
					continue;
				}
				for (auto k = tmp->getQueueItems().begin(); k != tmp->getQueueItems().end(); ++k) {
					QueueItem* q = *k;
					if(q->getPriority() == QueueItem::PAUSED || q->countOnlineUsers() >= (size_t)SETTING(MAX_AUTO_MATCH_SOURCES))
						continue;
					if(q->isRunning()) {
						//it's ok but see if we can find better
						cand = tmp;
					} else {
						//LogManager::getInstance()->message("BUNDLE1 " + tmp->getName() + " CHOSEN FROM PRIO " + AirUtil::getPrioText(p) + ", rand " + Util::toString(rand));
						return tmp;
					}
				}
				s++;
			}
		}
		rand -= (int)(p-1)*size;
		p++;
	} while(p < Bundle::LAST);

	//LogManager::getInstance()->message("BUNDLE2");
	return cand;
}

BundlePtr BundleQueue::find(const string& bundleToken) {
	auto i = bundles.find(bundleToken);
	if (i != bundles.end()) {
		return i->second;
	}
	return NULL;
}

BundlePtr BundleQueue::findDir(const string& aPath) {
	string dir = AirUtil::getReleaseDir(aPath);
	if (dir.empty()) {
		return NULL;
	}

	auto i = bundleDirs.find(dir);
	if (i != bundleDirs.end()) {
		return i->second;
	}
	return NULL;
}

void BundleQueue::getInfo(const string& aSource, BundleList& retBundles, int& finishedFiles, int& fileBundles) {
	BundlePtr tmpBundle;
	bool subFolder = false;
	{
		for (auto j = bundles.begin(); j != bundles.end(); ++j) {
			tmpBundle = j->second;
			if (tmpBundle->isFinished()) {
				continue;
			}
			if (tmpBundle->getTarget().find(aSource) != string::npos) {
				retBundles.push_back(tmpBundle);
				if (tmpBundle->getFileBundle()) {
					fileBundles++;
				}
			} else if (!tmpBundle->getFileBundle()) {
				//check subfolders in case we are splitting the bundle
				for (auto s = tmpBundle->getBundleDirs().begin(); s != tmpBundle->getBundleDirs().end(); ++s) {
					if (s->second != 0 && s->first.find(aSource) != string::npos) {
						retBundles.push_back(tmpBundle);
						subFolder = true;
						break;
					}
				}
			}
			if (subFolder) {
				break;
			}
		}
	}

	for (auto j = retBundles.begin(); j != retBundles.end(); ++j) {
		tmpBundle = *j;
		if (tmpBundle->getFileBundle()) {
			//filebundles can't have finished files...
			continue;
		}

		if (subFolder) {
			for_each(tmpBundle->getFinishedFiles().begin(), tmpBundle->getFinishedFiles().end(), [&](QueueItem* qi) { if(qi->getTarget().find(aSource) != string::npos) finishedFiles++; } );
			return;
		}

		finishedFiles += tmpBundle->getFinishedFiles().size();
	}
	return;
}

BundlePtr BundleQueue::getMergeBundle(const string& aTarget) {
	BundlePtr compareBundle;
	for (auto j = bundles.begin(); j != bundles.end(); ++j) {
		BundlePtr compareBundle = (*j).second;
		if (compareBundle->getFileBundle() || compareBundle->isFinished()) {
			continue;
		}

		if (compareBundle->getTarget().length() >= aTarget.length() && (stricmp(compareBundle->getTarget().substr(0, aTarget.length()), aTarget) == 0)) { //are we trying to merge a parent folder?
			return compareBundle;
		} else if (stricmp(aTarget.substr(0, compareBundle->getTarget().length()), compareBundle->getTarget()) == 0) { //subdir of the current bundle?
			return compareBundle;
		}
	}
	return NULL;
}

void BundleQueue::addBundleItem(QueueItem* qi, BundlePtr aBundle) {
	if (aBundle->addQueue(qi) && !aBundle->getFileBundle()) {
		string dir = Util::getDir(qi->getTarget(), false, false);
		string releaseDir = AirUtil::getReleaseDir(dir);
		if (!releaseDir.empty()) {
			bundleDirs[releaseDir] = aBundle;
		}
	}
}

void BundleQueue::removeBundleItem(QueueItem* qi, bool finished) {
	if (qi->getBundle()->removeQueue(qi, finished) && !finished && !qi->getBundle()->getFileBundle()) {
		string releaseDir = AirUtil::getReleaseDir(Util::getDir(qi->getTarget(), false, false));
		if (!releaseDir.empty()) {
			bundleDirs.erase(releaseDir);
		}
	}
}

void BundleQueue::remove(BundlePtr aBundle, bool finished) {
	if (finished && !aBundle->getFileBundle()) {
		for (auto i = aBundle->getBundleDirs().begin(); i != aBundle->getBundleDirs().end(); ++i) {
			//dcassert(i->second == 0);
			string releaseDir = AirUtil::getReleaseDir(i->first);
			if (!releaseDir.empty()) {
				bundleDirs.erase(releaseDir);
			}
		}
	}
	bundles.erase(aBundle->getToken());
}

void BundleQueue::move(BundlePtr aBundle, const string& newTarget) {
	//remove the old release dir
	string releaseDir = AirUtil::getReleaseDir(aBundle->getTarget());
	if (!releaseDir.empty()) {
		bundleDirs.erase(releaseDir);
	}

	aBundle->setTarget(newTarget);

	//add new
	releaseDir = AirUtil::getReleaseDir(aBundle->getTarget());
	if (!releaseDir.empty()) {
		bundleDirs[releaseDir] = aBundle;
	}
}

void BundleQueue::getAutoPrioMap(bool verbose, multimap<int, BundlePtr>& finalMap, int& uniqueValues) {
	//get bundles with auto priority
	boost::unordered_map<BundlePtr, double, Bundle::Hash> autoPrioMap;
	multimap<double, BundlePtr> sizeMap;
	multimap<int64_t, BundlePtr> timeMap;
	{
		for (auto i = bundles.begin(); i != bundles.end(); ++i) {
			BundlePtr bundle = i->second;
			if (bundle->getAutoPriority() && !bundle->isFinished()) {
				auto p = bundle->getPrioInfo();
				timeMap.insert(make_pair(p.first, bundle));
				sizeMap.insert(make_pair(p.second, bundle));
				autoPrioMap[bundle] = 0;
				/*if (verbose) {
					LogManager::getInstance()->message("Bundle " + bundle->getName() + ", time left " + Util::formatTime(p.first) + ", size factor " + Util::toString(p.second));
				} */
			}
		}
	}

	if (autoPrioMap.size() <= 1) {
		if (verbose) {
			LogManager::getInstance()->message("Not enough bundles with autoprio to calculate anything!");
		}
		return;
	}

	//scale the priorization maps
	double factor;
	double max = max_element(timeMap.begin(), timeMap.end())->first;
	if (max) {
		double factor = 100 / max;
		for (auto i = timeMap.begin(); i != timeMap.end(); ++i) {
			autoPrioMap[i->second] = i->first * factor;
		}
	}

	max = max_element(sizeMap.begin(), sizeMap.end())->first;
	if (max > 0) {
		factor = 100 / max;
		for (auto i = sizeMap.begin(); i != sizeMap.end(); ++i) {
			autoPrioMap[i->second] += i->first * factor;
		}
	}

	{
		//prepare to set the prios
		multimap<int, BundlePtr> finalMap;
		for (auto i = autoPrioMap.begin(); i != autoPrioMap.end(); ++i) {
			if (finalMap.find(i->second) == finalMap.end()) {
				uniqueValues++;
			}
			finalMap.insert(make_pair(i->second, i->first));
		}
	}
}

BundlePtr BundleQueue::findSearchBundle(uint64_t aTick, bool force /* =false */) {
	BundlePtr bundle = NULL;
	if((BOOLSETTING(AUTO_SEARCH) && (aTick >= nextSearch) && (bundles.size() > 0)) || force) {
		bundle = findAutoSearch();
		//LogManager::getInstance()->message("Next search in " + Util::toString(next) + " minutes");
	} 
	
	if(!bundle && (BOOLSETTING(AUTO_SEARCH) && (aTick >= nextRecentSearch) || force)) {
		bundle = findRecent();
		//LogManager::getInstance()->message("Next recent search in " + Util::toString(recentBundles > 1 ? 5 : 10) + " minutes");
	}

	if(bundle != NULL) {
		if (!bundle->getRecent()) {
			calculations++;
			switch((int)bundle->getPriority()) {
				case 2:
					lowSel++;
					break;
				case 3:
					normalSel++;
					break;
				case 4:
					highSel++;
					break;
				case 5:
					highestSel++;
					break;
			}
		} else {
			//LogManager::getInstance()->message("Performing search for a RECENT bundle: " + bundle->getName());
		}
		//LogManager::getInstance()->message("Calculations performed: " + Util::toString(calculations) + ", highest: " + Util::toString(((double)highestSel/calculations)*100) + "%, high: " + Util::toString(((double)highSel/calculations)*100) + "%, normal: " + Util::toString(((double)normalSel/calculations)*100) + "%, low: " + Util::toString(((double)lowSel/calculations)*100) + "%");
	}
	return bundle;
}

int64_t BundleQueue::recalculateSearchTimes(BundlePtr aBundle) {
	if (!aBundle->getRecent()) {
		int prioBundles = 0;
		getPrioSum(prioBundles);
		int next = SETTING(SEARCH_TIME);
		if (prioBundles > 0) {
			next = max(60 / prioBundles, next);
		}
		nextSearch = GET_TICK() + (next * 60 * 1000);
		return nextSearch;
	}
		
	int recentBundles = getRecentSize();
	nextRecentSearch = GET_TICK() + ((recentBundles > 1 ? 5 : 10) * 60 * 1000);
	return nextRecentSearch;
}

void BundleQueue::getDiskInfo(map<string, pair<string, int64_t>>& dirMap, const StringSet& volumes) {
	string tempVol;
	bool useSingleTempDir = (SETTING(TEMP_DOWNLOAD_DIRECTORY).find("%[targetdrive]") == string::npos);
	if (useSingleTempDir) {
		tempVol = AirUtil::getMountPath(SETTING(TEMP_DOWNLOAD_DIRECTORY), volumes);
	}

	for (auto i = bundles.begin(); i != bundles.end(); ++i) {
		BundlePtr b = (*i).second;
		string mountPath = AirUtil::getMountPath(b->getTarget(), volumes);
		if (!mountPath.empty()) {
			auto s = dirMap.find(mountPath);
			if (s != dirMap.end()) {
				bool countAll = (useSingleTempDir && (mountPath != tempVol));
				s->second.second -= b->getDiskUse(countAll);
			}
		}
	}
}

void BundleQueue::saveQueue(bool force) noexcept {
	try {
		for (auto i = bundles.begin(); i != bundles.end(); ++i) {
			BundlePtr b = i->second;
			if (!b->isFinished() && (b->getDirty() || force)) {
				b->save();
			}
		}
	} catch(...) {
		// ...
	}
}

} //dcpp