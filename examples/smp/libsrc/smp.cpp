﻿// --------------------------------------------
// Copyright KAPSARC. Open source MIT License.
// --------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2015 King Abdullah Petroleum Studies and Research Center
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom
// the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// --------------------------------------------
//
// Demonstrate a very basic, but highly parameterizable, Spatial Model of Politics.
//
// --------------------------------------------

#include "smp.h"

namespace SMPLib {
using std::cout;
using std::endl;
using std::flush;
using std::function;
using std::get;
using std::string;

using KBase::PRNG;
using KBase::KMatrix;
using KBase::KException;
using KBase::Actor;
using KBase::Model;
using KBase::Position;
using KBase::VctrPstn;
using KBase::BigRAdjust;
using KBase::BigRRange;
using KBase::VPModel;
using KBase::State;
using KBase::StateTransMode;
using KBase::VotingRule;
using KBase::PCEModel;
using KBase::ReportingLevel;


// --------------------------------------------

// big enough buffer to build all desired SQLite statements
const unsigned int sqlBuffSize = 250;

// --------------------------------------------

// this binds the given parameters and returns the λ-fn necessary to stop the SMP appropriately
function<bool(unsigned int, const State *)>
smpStopFn(unsigned int minIter, unsigned int maxIter, double minDeltaRatio, double minSigDelta) {
    auto  sfn = [minIter, maxIter, minDeltaRatio, minSigDelta](unsigned int iter, const State * s) {
        bool tooLong = (maxIter <= iter);
        bool longEnough = (minIter <= iter);
        bool quiet = false;
        auto sf = [](unsigned int i1, unsigned int i2, double d12) {
            printf("sDist [%2i,%2i] = %.2E   ", i1, i2, d12);
            return;
        };
        auto s0 = ((const SMPState*)(s->model->history[0]));
        auto s1 = ((const SMPState*)(s->model->history[1]));
        auto d01 = SMPModel::stateDist(s0, s1) + minSigDelta;
        sf(0, 1, d01);
        auto sx = ((const SMPState*)(s->model->history[iter - 0]));
        auto sy = ((const SMPState*)(s->model->history[iter - 1]));
        auto dxy = SMPModel::stateDist(sx, sy);
        sf(iter - 1, iter - 0, dxy);
        const double aRatio = dxy / d01;
        quiet = (aRatio < minDeltaRatio);
        printf("\nFractional change compared to first step: %.4f  (target=%.4f) \n\n", aRatio, minDeltaRatio);
        return tooLong || (longEnough && quiet);
    };
    return sfn;
};

// --------------------------------------------

SMPActor::SMPActor(string n, string d) : Actor(n, d) {
    vr = VotingRule::Proportional; // just a default
}

SMPActor::~SMPActor() {
}

double SMPActor::vote(unsigned int est, unsigned int i, unsigned int j, const State*st) const {
    unsigned int k = st->model->actrNdx(this);
    auto uk = st->aUtil[est];
    double uhki = uk(k, i);
    double uhkj = uk(k, j);
    const double vij = Model::vote(vr, sCap, uhki, uhkj);
    return vij;
}


double SMPActor::vote(const Position * ap1, const Position * ap2, const SMPState* ast) const {
    double u1 = posUtil(ap1, ast);
    double u2 = posUtil(ap2, ast);
    double v = Model::vote(vr, sCap, u1, u2);
    return v;
}


double SMPActor::posUtil(const Position * ap1, const SMPState* as) const {
    assert(nullptr != as);
    int ai = as->model->actrNdx(this);
    double ri = as->aNRA(ai); //as->nra(ai, 0);
    assert(0 <= ai);
    const VctrPstn* p0 = ((const VctrPstn*)(as->pstns[ai]));
    assert(nullptr != p0);
    auto p1 = ((const VctrPstn*)ap1);
    assert(nullptr != p1);
    double u1 = SMPModel::bvUtil((*p0) - (*p1), vSal, ri);
    return u1;
}


void SMPActor::randomize(PRNG* rng, unsigned int numD) {
    sCap = rng->uniform(10.0, 200.0);

    // assign an overall salience, and then by-component saliences
    double s = rng->uniform(0.75, 0.99);
    vSal = KMatrix::uniform(rng, numD, 1, 0.1, 1.0);
    vSal = (s * vSal) / sum(vSal);
    assert(fabs(s - sum(vSal)) < 1E-4);

    // Note that we randomly assign different voting rules
  //vr = VotingRule::Proportional;
    const unsigned int numVR = KBase::VotingRuleNames.size();
    const unsigned int vrNum = ((unsigned int)(rng->uniform(0.0, numVR -0.01)));
    vr = VotingRule(vrNum);
    //cout << "Voting rule " << vrNum << "  " << vr << endl;
    return;
}



void SMPActor::interpBrgnSnPm(unsigned int n, unsigned int m,
                              double tik, double sik, double prbI,
                              double tjk, double sjk, double prbJ,
                              double & bik, double & bjk) {

    auto round4 = [](const double x1) {
        const double s = 10000.0;
        const int y = ((int)(0.5 + (x1*s)));
        double x2 = ((double)y) / s;
        return x2;
    };
    assert((1 == n) || (2 == n));
    assert((1 == m) || (2 == m));

    double wsi = pow(sik, n);
    double wpi = pow(prbI, m);
    double wik = wsi * wpi;

    double wsj = pow(sjk, n);
    double wpj = pow(prbJ, m);
    double wjk = wsj * wpj;

    // imagine that either neither actor cares, or neither actor can coerce the other,
    // so that wik = 0 = wjk. We need to avoid 0/0 error, and have bi=ti and bj=tj.
    // Thus, the asymmetry is intentional when wik = 0 = wjk.
    // to avoid spurious asymmetry in other cases, and spurious precision always, round to 4 decimals
    const double minW = 1e-6;
    bik = round4(((wik + minW)*tik + wjk*tjk) / (wik + minW + wjk));
    bjk = round4((wik*tik + (minW + wjk)*tjk) / (wik + minW + wjk));

    return;
}


void SMPActor::interpBrgnS2PMax(double tik, double sik, double prbI,
                                double tjk, double sjk, double prbJ,
                                double & bik, double & bjk) {
    double di = (prbJ > prbI) ? (prbJ - prbI) : 0;  // max(0, prbJ - prbI);
    double dj = (prbI > prbJ) ? (prbI - prbJ) : 0;  // max(0, prbI - prbJ);
    double sik2 = sik * sik;
    double sjk2 = sjk * sjk;

    const double minW = 1e-6;
    double dik = (di * sjk2) / ((di * sjk2) + minW + ((1 - di) * sik2));
    double djk = (dj * sik2) / ((dj * sik2) + minW + ((1 - dj) * sjk2));


    bik = tik + dik * (tjk - tik);
    bjk = tjk + djk * (tik - tjk);
    return;
}


BargainSMP* SMPActor::interpolateBrgn(const SMPActor* ai, const SMPActor* aj,
                                      const VctrPstn* posI, const VctrPstn * posJ,
                                      double prbI, double prbJ, InterVecBrgn ivb) {
    assert((1 == posI->numC()) && (1 == posJ->numC()));
    unsigned int numD = posI->numR();
    assert(numD == posJ->numR());
    auto brgnI = VctrPstn(numD, 1);
    auto brgnJ = VctrPstn(numD, 1);

    for (unsigned int k = 0; k < numD; k++) {
        double tik = (*posI)(k, 0);
        double sik = ai->vSal(k, 0);

        double tjk = (*posJ)(k, 0);
        double sjk = aj->vSal(k, 0);
        double & bik = tik;
        double & bjk = tjk;
        switch (ivb) {
        case InterVecBrgn::S1P1:
            interpBrgnSnPm(1, 1, tik, sik, prbI, tjk, sjk, prbJ, bik, bjk);
            break;
        case InterVecBrgn::S2P2:
            interpBrgnSnPm(2, 2, tik, sik, prbI, tjk, sjk, prbJ, bik, bjk);
            break;
        case InterVecBrgn::S2PMax:
            interpBrgnS2PMax(tik, sik, prbI, tjk, sjk, prbJ, bik, bjk);
            break;
        default:
            throw KException("interpolateBrgn: unrecognized InterVecBrgn value");
            break;
        }
        brgnI(k, 0) = bik;
        brgnJ(k, 0) = bjk;
    }

    auto brgn = new BargainSMP(ai, aj, brgnI, brgnJ);
    return brgn;
}



SMPState::SMPState(Model * m) : State(m) {
    nra = KMatrix();

    // cannot  setup AccomdateMatrix yet, because we have no actors
    accomodate = KMatrix();
}


SMPState::~SMPState() {
    nra = KMatrix();
    ideals = {};
    accomodate = KMatrix();
}

void SMPState::setVDiff(const vector<VctrPstn> & vPos) {
    auto dfn = [vPos, this](unsigned int i, unsigned int j) {
        auto ai = ((const SMPActor*)(model->actrs[i]));
        KMatrix si = ai->vSal;
        auto posJ = ((const VctrPstn*)(pstns[j]));
        double dij = 0.0;
        if (0 == vPos.size()) {
            auto posI = ((const VctrPstn*)(pstns[i]));
            auto idlI = ideals[i];
            //dij = SMPModel::bvDiff((*posI) - (*posJ), si);
            dij = SMPModel::bvDiff(idlI - (*posJ), si);
        }
        else {
            auto vpi = vPos[i];
            dij = SMPModel::bvDiff(vpi - (*posJ), si);
        }
        return dij;
    };

    const unsigned int na = model->numAct;
    assert(na == ideals.size());
    assert(na == accomodate.numR());
    assert(na == accomodate.numC());
    vDiff = KMatrix::map(dfn, na, na);
    return;
}

/*
void SMPModel::setUtilProb(const KMatrix& vR, const KMatrix& vS, const KMatrix& vD,
  KBase::VotingRule vr, KBase::VPModel vpm) {
  // vR(i,0) = risk attitude used for actor i (possibly 0, possibly someone's estimate)
  // vS(i,0) = overall salience for actor i
  // vD(i,j) = difference, using i's salience, between i's position and j's position
  const unsigned int na = vD.numR();
  assert(na == vD.numC());
  assert(na == vR.numR());
  assert(1 == vR.numC());
  assert(KBase::sameShape(vR, vS));

  // calculate utility matrix
  // utils(i,j) = utility to actor i of position of actor j
  auto uFn = [vR, vD](unsigned int i, unsigned int j) {
    const double ri = vR(i, 0);
    const double dij = vD(i, j);
    const double uij = bsUtil(dij, ri);
    return uij;
  };
  auto utils = KMatrix::map(uFn, na, na);
  const double aeTol = 1E-8; // arithmetic error tolerance
  const double minCS = 1E-10; // avoid 0/0 errors

  // calculate coalition strengths
  // cs(i,j) = strength of actor i's position against actor j's position
  auto cs = KMatrix(na, na, minCS);
  for (unsigned int i = 0; i < na; i++) {
    for (unsigned int j = 0; j < i; j++) {
      for (unsigned int k = 0; k < na; k++) {
        double sk = vS(k, 0);
        assert(0 < sk);
        assert(sk <= 1.0);
        double uki = utils(k, i);
        double ukj = utils(k, j);
        double vkij = Model::vote(vr, sk, uki, ukj);
        double vkji = Model::vote(vr, sk, ukj, uki);
        assert(fabs(vkij + vkji) < aeTol); // these should exactly cancel
        if (0 < vkij) {
          cs(i, j) = cs(i, j) + vkij;
          assert(0 < cs(i, j));
        }
        if (0 < vkji) {
          cs(j, i) = cs(j, i) + vkji;
          assert(0 < cs(j, i));
        }
      }
    }
  }

  // calculate pairwise victory probabilities
  //auto vpm = VPModel::Linear;
  auto vP = KMatrix(na, na);
  for (unsigned int i = 0; i < na; i++) {
    for (unsigned int j = 0; j < i; j++) {
      //double pij = cs(i, j) / (cs(i, j) + cs(j, i)); // was never used
      auto ppr = vProb(vpm, cs(i, j), cs(j, i));
      vP(i, j) = get<0>(ppr); // set the lower left  probability: if Linear, cij / (cij + cji)
      vP(j, i) = get<1>(ppr); // set the upper right probability: if Linear, cji / (cij + cji)
    }
  }
  for (unsigned int i = 0; i < na; i++) { // check arithmetic
    for (unsigned int j = 0; j < na; j++) {
      assert(fabs(vP(i, j) + vP(j, i) - 1.0) < aeTol);
    }
  }
  return;
};

*/

double SMPState::estNRA(unsigned int h, unsigned int i, BigRAdjust ra) const {
    double rh = nra(h, 0);
    double ri = nra(i, 0);
    double rhi = Model::estNRA(rh, ri, ra);
    return rhi;
}

KMatrix SMPState::actrCaps() const {
    auto wFn = [this](unsigned int i, unsigned int j) {
        auto aj = ((SMPActor*)(model->actrs[j]));
        return aj->sCap;

    };
    auto w = KMatrix::map(wFn, 1, model->numAct);
    return w;
}


void SMPState::setAllAUtil(ReportingLevel rl) {
    const auto vpmCoalition = model->vpm;
    const unsigned int na = model->numAct;
    auto smod = (const SMPModel*)model;
    const auto vrCoalition = smod->vrCltn;
    const auto ra = smod->bigRAdj;
    const auto rr = smod->bigRRng;

    // make sure prerequisities are at least somewhat setup
    assert(na == eIndices.size());
    assert(0 < uIndices.size());
    assert(uIndices.size() <= na);

    auto w_j = actrCaps();
    setVDiff();
    nra = KMatrix(na, 1); // zero-filled, i.e. risk neutral
    auto uFn1 = [this](unsigned int i, unsigned int j) {
        return  SMPModel::bsUtil(vDiff(i, j), nra(i, 0));
    };

    auto rnUtil_ij = KMatrix::map(uFn1, na, na);

    if (ReportingLevel::Silent < rl) {
        cout << "Raw actor-pos value matrix (risk neutral)" << endl;
        rnUtil_ij.mPrintf(" %+.3f ");
        cout << endl << flush;
    }


    auto vfn = [vrCoalition, &w_j, &rnUtil_ij](unsigned int k, unsigned int i, unsigned int j) {
        double vkij = Model::vote(vrCoalition, w_j(0, k), rnUtil_ij(k, i), rnUtil_ij(k, j));
        return vkij;
    };
    const auto c = Model::coalitions(vfn, na, na); // c(i,j) = strength of coaltion for i against j
    const auto pv2 = Model::probCE2(model->pcem, vpmCoalition, c);
    const auto p_i = get<0>(pv2); // column
    const auto pv_ij = get<1>(pv2); // square
    nra = Model::bigRfromProb(p_i, rr);

    if (ReportingLevel::Silent < rl) {
        cout << "Inferred risk attitudes: " << endl;
        nra.mPrintf(" %+.3f ");
        cout << endl << flush;
    }

    auto raUtil_ij = KMatrix::map(uFn1, na, na);

    if (ReportingLevel::Silent < rl) {
        cout << "Risk-aware actor-pos utility matrix (objective):" << endl;
        raUtil_ij.mPrintf(" %+.4f ");
        cout << endl;
        cout << "RMS change in value vs utility: " << norm(rnUtil_ij - raUtil_ij) / na << endl << flush;
    }

    const double duTol = 1E-6;
    assert(duTol < norm(rnUtil_ij - raUtil_ij)); // I've never seen it below 0.07


    if (ReportingLevel::Silent < rl) {
        switch (ra) {
        case BigRAdjust::FullRA:
            cout << "Using " << ra << ": r^h_i = ri" << endl;
            break;
        case BigRAdjust::TwoThirdsRA:
            cout << "Using " << ra << ": r^h_i = (rh + 2*ri)/3" << endl;
            break;
        case BigRAdjust::HalfRA:
            cout << "Using " << ra << ": r^h_i = (rh + ri)/2" << endl;
            break;
        case BigRAdjust::OneThirdRA:
            cout << "Using " << ra << ": r^h_i = (2*rh + ri)/3" << endl;
            break;
        case BigRAdjust::NoRA:
            cout << "Using " << ra << ": r^h_i = rh " << endl;
            break;
        default:
            cout << "Unrecognized BigRAdjust" << endl;
            assert(false);
            break;
        }
    }

    aUtil = vector<KMatrix>();
    for (unsigned int h = 0; h < na; h++) {
        auto u_h_ij = KMatrix(na, na);
        for (unsigned int i = 0; i < na; i++) {
            double rhi = estNRA(h, i, ra);
            for (unsigned int j = 0; j < na; j++) {
                double dij = vDiff(i, j);
                u_h_ij(i, j) = SMPModel::bsUtil(dij, rhi);
            }
        }
        aUtil.push_back(u_h_ij);


        if (ReportingLevel::Silent < rl) {
            cout << "Estimate by " << h << " of risk-aware utility matrix:" << endl;
            u_h_ij.mPrintf(" %+.4f ");
            cout << endl;

            cout << "RMS change in util^h vs utility: " << norm(u_h_ij - raUtil_ij) / na << endl;
            cout << endl;
        }

        assert(duTol < norm(u_h_ij - raUtil_ij)); // I've never seen it below 0.03
    }
    return;
}


void SMPState::setOneAUtil(unsigned int perspH, ReportingLevel rl) {
    cout << "SMPState::setOneAUtil - not yet implemented" << endl << flush;


    return;
}

void SMPState::showBargains(const vector < vector < BargainSMP* > > & brgns) const {
    for (unsigned int i = 0; i < brgns.size(); i++) {
        printf("Bargains involving actor %2u: ", i);
        for (unsigned int j = 0; j < brgns[i].size(); j++) {
            BargainSMP* bij = brgns[i][j];
            assert(nullptr != bij);
            showOneBargain(bij);
        }
        cout << endl << flush;
    }
    return;
}

void SMPState::showOneBargain(const BargainSMP* b) const {
    assert(nullptr != b);
    unsigned int ai = model->actrNdx(b->actInit);
    unsigned int aj = model->actrNdx(b->actRcvr);
    uint64_t bid = b->getID();
    printf("[%llu, %u:%u]", bid, ai, aj);
    return;
}

void SMPState::setNRA() {
    const unsigned int nr = nra.numR();
    nra = KMatrix(nr, 1);
    return;
}

double SMPState::aNRA(unsigned int i) const {
    const unsigned int nr = nra.numR();
    assert(nr == model->numAct);
    assert(i < nr);
    const double ri = nra(i, 0);
    return ri;
}

void  SMPState::setAccomodate(const KMatrix & aMat) {
    const unsigned int na = model->numAct;
    assert(Model::minNumActor <= na);
    assert(na <= Model::maxNumActor);
    assert(na == aMat.numR());
    assert(na == aMat.numC());
    accomodate = aMat;
    identAccMat = KBase::iMatP(accomodate);
    return;
}

VctrPstn SMPState::getIdeal(unsigned int n) const
{
	return ideals[n];
}
void SMPState::addPstn(Position* ap) {
    auto sp = (VctrPstn*)ap;
    auto sm = (SMPModel*)model;
    assert(1 == sp->numC());
    assert(sm->numDim == sp->numR());

    State::addPstn(ap);
    return;
}

bool SMPState::equivNdx(unsigned int i, unsigned int j) const {
    /// Compare two actual positions in the current state
    auto vpi = ((const VctrPstn *)(pstns[i]));
    auto vpj = ((const VctrPstn *)(pstns[j]));
    assert(vpi != nullptr);
    assert(vpj != nullptr);
    double diff = norm((*vpi) - (*vpj));
    auto sm = ((const SMPModel*)model);
    bool rslt = (diff < sm->posTol);
    return rslt;
}


// set the diff matrix, do probCE for risk neutral,
// estimate Ri, and set all the aUtil[h] matrices
SMPState* SMPState::stepBCN() {
    auto gSetup = [](SMPState* s) {
        if ((0 == s->uIndices.size()) || (0 == s->eIndices.size())) {
            s->setUENdx();
        }
        if (0 == s->aUtil.size()) {
            s->setAUtil(-1, ReportingLevel::Low);
        }
        return;
    };
    gSetup(this);
    int myT = myTurn();

    // JAH 20160802 toggle population of PosUtil, PosEquiv, PosVote, and PosBrob
    // en masse based on value at index 1 of the sqlFlags vector
    // VectorPosition, which is in this same group, is handled separately
    if (model->sqlFlags[1])
    {
        model->sqlAUtil(myT);
        model->sqlPosEquiv(myT);
        model->sqlPosProb(myT);
        model->sqlPosVote(myT);
    }
    // That gets recorded upon the next state - but it
    // therefore misses the very last state.
    auto s2 = doBCN();
    gSetup(s2);
    s2->step = [s2]() {
        return s2->stepBCN();
    };
    return s2;
}

void SMPState::newIdeals() {
    const unsigned int na = model->numAct;
    const double tol = 1E-10;

    assert(Model::minNumActor <= na);
    assert(na <= Model::maxNumActor);
    assert(na == accomodate.numC());
    assert(na == accomodate.numR());
    assert(na == ((unsigned int)(ideals.size())));

    const bool identP = (KBase::norm(accomodate - KBase::iMat(na)) < tol);

    const unsigned int nDim = ((SMPModel*)model)->numDim;

    vector<VctrPstn> nIdeals = {};

    auto posK = [this](unsigned int k) {
        auto ppK = ((const VctrPstn*)(pstns[k]));
        const KMatrix pK = KMatrix(*ppK);
        return pK;
    };

    for (unsigned int i = 0; i < na; i++) {
        double si = 0.0;
        auto pI = posK(i);
        auto newIP = KMatrix(nDim, 1); // new ideal point
        for (unsigned int j = 0; j < na; j++) {
            const double aij = accomodate(i, j); // save typing
            assert(0 <= aij);
            assert(aij <= 1.0);
            si = si + aij;
            assert(si <= 1.0 + tol); // cannot be more than slightly above at any point
            auto pJ = posK(j);
            newIP = newIP + (aij * pJ);

            // very temporary!!
            if (identP && (i == j)) {
                assert(fabs(aij - 1.0) < tol);
            }
        }
        si = (1.0 < si) ? 1.0 : si; // clip to 1, if slightly above
        double lagI = 1.0 - si;
        assert(0.0 <= lagI);
        assert(lagI <= 1.0);
        if (identP) {
            assert(fabs(lagI) < tol);
        }
        newIP = newIP + (lagI * ideals[i]);
        if (identP) {
            assert(KBase::norm(newIP - pI) < tol);
        }
        nIdeals.push_back(VctrPstn(newIP));
    }

    ideals = nIdeals;

    if (identP) {
        assert(posIdealDist() < tol);
    }

    return;
}

void SMPState::idealsFromPstns(const vector<VctrPstn> &  ps) {
    const unsigned int na = model->numAct;
    assert(Model::minNumActor <= na);
    assert(na <= Model::maxNumActor);

    const bool givenP = (na == ps.size());
    assert(givenP || (0 == ps.size()));

    ideals = {};

    for (unsigned int i = 0; i < na; i++) {
        if (givenP) {
            ideals.push_back(ps[i]);
        }
        else {
            auto ppJ = ((const VctrPstn*)(pstns[i]));
            auto newIP = VctrPstn(*ppJ);
            ideals.push_back(VctrPstn(newIP));
        }
    }

    return;
}

double SMPState::posIdealDist(ReportingLevel rl) const {
    const unsigned int t = 0; // myTurn();
    double rmsDist = 0.0;
    const unsigned int na = model->numAct;
    assert(na == pstns.size());
    assert(na == ideals.size());
    for (unsigned int i = 0; i < na; i++) {
        auto ppI = ((const VctrPstn*)(pstns[i]));
        const KMatrix pI = KMatrix(*ppI);
        auto iI = ideals[i];

        if (rl > ReportingLevel::Low) {
            printf("postn %2u, %2u ", i, t);
            trans(pI).mPrintf(" %.4f ");
            printf("ideal %2u, %2u ", i, t);
            trans(iI).mPrintf(" %.4f ");
        }
        double dI = KBase::norm(pI - iI);
        if (rl > ReportingLevel::Silent) {
            printf("postn-ideal distance %2u, %2u: %.5f \n", i, t, dI);
        }
        rmsDist = rmsDist + (dI*dI);
    }
    rmsDist = rmsDist / ((double)na);
    rmsDist = sqrt(rmsDist);
    if (rl > ReportingLevel::Silent) {
        printf("postn-ideal distance RMS %2u: %.5f \n", t, rmsDist);
        cout << flush;
    }
    return rmsDist;
}

void SMPState::setAccomodate(double adjRate) {

    // a man's gotta know his limits
    // (with apologies to HC)
    assert(0.0 <= adjRate);
    assert(adjRate <= 1.0);
    const unsigned int na = model->numAct;

    printf("Setting SMPState::accomodate to %.3f * identity matrix \n", adjRate);

    // A standard Identity matrix is helpful here because it
    // should keep the behavior same as the original "cynical" model:
    //      ideal_{i,t} := pstn_{i,t}
    auto am = adjRate * KBase::iMat(na);
    setAccomodate(am);
    return;
}

tuple< KMatrix, VUI> SMPState::pDist(int persp) const {
    /// Calculate the probability distribution over states from this perspective

    // TODO: convert this to a single, commonly used setup function
    const VotingRule vr = VotingRule::Proportional;
    const ReportingLevel rl = ReportingLevel::Silent;

    const unsigned int na = model->numAct;
    const KMatrix w = actrCaps();

    auto uij = KMatrix(na, na); // full utility matrix, including duplicate columns
    assert(na == aUtil.size()); // must have been filled in
    if ((0 <= persp) && (persp < na)) {
        uij = aUtil[persp];
    }
    else if (-1 == persp) {
        for (unsigned int i = 0; i < na; i++) {
            for (unsigned int j = 0; j < na; j++) {
                auto ui = aUtil[i];
                uij(i, j) = aUtil[i](i, j);
            }
        }
    }
    else {
        cout << "SMPState::pDist: unrecognized perspective, " << persp << endl << flush;
        assert(false);
    }

    assert(0 < uIndices.size()); // should have been set with setUENdx();
    //auto uNdx2 = uniqueNdx(); // get the indices to unique positions
    if (ReportingLevel::Silent < rl) {
        printf("Unique positions %i/%u ", uIndices.size(), na);
        cout << "[ ";
        for (auto i : uIndices) {
            printf(" %i ", i);
        }
        cout << " ] " << endl << flush;
    }
    auto uufn = [uij, this](unsigned int i, unsigned int j) {
        return uij(i, uIndices[j]);
    };
    auto uUij = KMatrix::map(uufn, na, uIndices.size());
    auto upd = Model::scalarPCE(na, uIndices.size(), w, uUij, vr, model->vpm, model->pcem, rl);

    return tuple< KMatrix, VUI>(upd, uIndices);
}


// -------------------------------------------------

string SMPModel::dbPath = "testsmp.db"; // Initializing to have testsmp.db as a default DB name

// JAH 20160711 added rng seed
SMPModel::SMPModel(string desc, uint64_t s, vector<bool> f) : Model(desc, s, f) {
    // note that numDim, posTol, and dimName are initialized in class declaration
    // TODO: get cleaner opening of smpDB
    sqlTest();
}

SMPModel::~SMPModel() {
    // TODO: probably should not close smpDB automatically.
    // With committee selection, we might have dozens of SMP models writing into one database,
    // so we cannot automatically close it when deleting a particular SMP.

    if (nullptr != smpDB) {
        cout << "SMPModel::~SMPModel Closing database" << endl << flush;
        int close_result = sqlite3_close(smpDB);
        if (close_result != SQLITE_OK) {
            cout << "SMPModel::~SMPModel Closing database failed!" << endl << flush;
        }
        else {
            cout << "SMPModel::~SMPModel Closing database succeeded." << endl << flush;
        }
        smpDB = nullptr;
    }

}



void SMPModel::addDim(string dn) {
    dimName.push_back(dn);
    numDim = dimName.size();
    return;
}

double SMPModel::stateDist(const SMPState* s1, const SMPState* s2) {
    unsigned int n = s1->pstns.size();
    assert(n == s2->pstns.size());
    double dSum = 0;
    for (unsigned int i = 0; i < n; i++) {
        auto vp1i = ((const VctrPstn*)(s1->pstns[i]));
        auto vp2i = ((const VctrPstn*)(s2->pstns[i]));
        dSum = dSum + KBase::norm((*vp1i) - (*vp2i));
    }
    return dSum;
}


// 0 <= d <= 1 is the difference in normalized position
// -1 <= R <= +1 is normalized risk-aversion
double SMPModel::bsUtil(double sd, double R) {
    double u = 0;
    assert(0 <= sd);
    if (sd <= 1) {
        u = (1 - sd)*(1 + sd*R);
    }
    else { // searches and round-off can drive sd > 1, slightly
        u = (1 - sd)*(1 + R);
    }
    return u;
}

double SMPModel::bvDiff(const  KMatrix & vd, const  KMatrix & vs) {
    assert(KBase::sameShape(vd, vs));
    double dsSqr = 0;
    double ssSqr = 0;
    for (unsigned int i = 0; i < vd.numR(); i++) {
        for (unsigned int j = 0; j < vd.numC(); j++) {
            const double dij = vd(i, j);
            const double sij = vs(i, j);
            assert(0 <= sij);
            const double ds = dij * sij;
            const double s = sij;
            dsSqr = dsSqr + (ds*ds);
            ssSqr = ssSqr + (s*s);
        }
    }
    assert(0 < ssSqr);
    double sd = sqrt(dsSqr / ssSqr);
    return sd;
};

double SMPModel::bvUtil(const  KMatrix & vd, const  KMatrix & vs, double R) {
    const double sd = bvDiff(vd, vs);
    const double u = bsUtil(sd, R);
    return u;
};

void SMPModel::sankeyOutput(string inputCSV) const {
    assert(numAct == actrs.size());
    assert(numDim == dimName.size());
    if (1 == numDim) {
        unsigned int nameLen = inputCSV.length();
        cout << endl;
        const char* appendEffPwr = "_effPow.csv";
        char* epName = newChars(nameLen + strlen(appendEffPwr) + 1);
        sprintf(epName, "%s%s", inputCSV.c_str(), appendEffPwr);
        cout << "Record effective power in " << epName << "  ...  " << flush;
        FILE* f1 = fopen(epName, "w");
        for (unsigned int i = 0; i < numAct; i++) {
            auto ai = ((const SMPActor*)actrs[i]);
            double ci = ai->sCap;
            assert(0.0 < ci);
            double si = KBase::sum(ai->vSal);
            assert(0.0 < si);
            assert(si <= 1.0);
            double epi = ci * si;
            fprintf(f1, "%s,%5.1f\n", ai->name.c_str(), epi);
        }
        fclose(f1);
        f1 = nullptr;
        cout << "done" << endl;
        delete epName;
        epName = nullptr;

        const char* appendPosLog = "_posLog.csv";
        char* plName = newChars(nameLen + strlen(appendPosLog) + 1);
        sprintf(plName, "%s%s", inputCSV.c_str(), appendPosLog);
        cout << "Record 1D positions over time, without dimension-name in " << plName << "  ...  " << flush;
        FILE* f2 = fopen(plName, "w");
        for (unsigned int i = 0; i < numAct; i++) {
            fprintf(f2, "%s", actrs[i]->name.c_str());
            for (unsigned int t = 0; t < history.size(); t++) {
                auto st = history[t];
                auto pit = st->pstns[i];
                auto vpit = (const VctrPstn*)pit;
                assert(1 == vpit->numC());
                assert(numDim == vpit->numR());
                fprintf(f2, ",%5.1f", 100 * (*vpit)(0, 0)); // have to print "100.0" sometimes
            }
            fprintf(f2, "\n");
        }
        fclose(f2);
        f2 = nullptr;
        cout << "done." << endl;
        delete plName;
        plName = nullptr;
    }
    return;
}

// JAH 20160801 changed to refer to model sqlFlags vector to decide
// whether or not to populate the table
void SMPModel::showVPHistory() const {
	assert(numAct == actrs.size());
	assert(numDim == dimName.size());

	// first need to get the group ID for this table
	// so then we can get the flag to populate the table or not
	// note the implicit assumption that there will never be 43+ groups :-)
	unsigned int grpID = 42;
	for (unsigned int t = 0; t<KTables.size(); t++)
	{
		if (KTables[t]->tabName == "VectorPosition")
		{
			grpID = KTables[t]->tabGrpID;
			break;
		}
	}
	// be sure that it found this table
	assert(grpID != 42);
	assert(grpID < sqlFlags.size());

	// JAH 20160801 only populate the table if this group is turned on
	if (sqlFlags[grpID])
	{
		assert(nullptr != smpDB);
		char* zErrMsg = nullptr;

		//createSQL(Model::NumTables + 0); // Make sure VectorPosition table is present
		auto sqlBuff = newChars(sqlBuffSize);
		sprintf(sqlBuff,
			"INSERT INTO VectorPosition (ScenarioId, Turn_t, Act_i, Dim_k, Pos_Coord, Idl_Coord) VALUES ('%s', ?1, ?2, ?3, ?4, ?5)",
			scenId.c_str());

		assert(nullptr != smpDB);
		const char* insStr = sqlBuff;
		sqlite3_stmt *insStmt;
		sqlite3_prepare_v2(smpDB, insStr, strlen(insStr), &insStmt, NULL);
		assert(nullptr != insStmt); //make sure it is ready

									// Prepared statements cache the execution plan for a query after the query optimizer has
									// found the best plan, so there is no big gain with simple insertions.
									// What makes a huge difference is bundling a few hundred into one atomic "transaction".
									// For this case, runtime droped from 62-65 seconds to 0.5-0.6 (vs. 0.30-0.33 with no SQL at all).

		sqlite3_exec(smpDB, "BEGIN TRANSACTION", NULL, NULL, &zErrMsg);

		// show positions over time
		for (unsigned int i = 0; i < numAct; i++) {
			for (unsigned int k = 0; k < numDim; k++) {
				printf("%s , %s , ", actrs[i]->name.c_str(), dimName[k].c_str());
				for (unsigned int t = 0; t < history.size(); t++) {
					auto st = history[t];
					auto pit = st->pstns[i];
					auto vpit = (const VctrPstn*)pit;
					auto sst = ((const SMPState*)st);
					auto vidl = sst->getIdeal(i);
					assert(1 == vpit->numC());
					assert(numDim == vpit->numR());
					printf("%5.1f , ", 100 * (*vpit)(k, 0)); // have to print "100.0" sometimes
					int rslt = 0;
					rslt = sqlite3_bind_int(insStmt, 1, t);
					assert(SQLITE_OK == rslt);
					rslt = sqlite3_bind_int(insStmt, 2, i);
					assert(SQLITE_OK == rslt);
					rslt = sqlite3_bind_int(insStmt, 3, k);
					assert(SQLITE_OK == rslt);
					const double pCoord = (*vpit)(k, 0);
					rslt = sqlite3_bind_double(insStmt, 4, pCoord);
					assert(SQLITE_OK == rslt);
					const double iCoord = vidl(k, 0);
					rslt = sqlite3_bind_double(insStmt, 5, iCoord);
					assert(SQLITE_OK == rslt);
					rslt = sqlite3_step(insStmt);
					assert(SQLITE_DONE == rslt);
					sqlite3_clear_bindings(insStmt);
					assert(SQLITE_DONE == rslt);
					rslt = sqlite3_reset(insStmt);
					assert(SQLITE_OK == rslt);
				}
				cout << endl;
			}
		}

		sqlite3_exec(smpDB, "END TRANSACTION", NULL, NULL, &zErrMsg);
		sqlite3_finalize(insStmt); // finalize statement to avoid resource leaks
		cout << endl;

		delete sqlBuff;
		sqlBuff = nullptr;
	}

	// show probabilities over time.
	// Note that we have to set the aUtil matrices for the last one.
	vector<KMatrix> prbHist = {};
	vector<VUI> unqHist = {};
	for (unsigned int t = 0; t < history.size(); t++) {
		auto sst = (SMPState*)history[t];
		assert(numAct == sst->aUtil.size()); // should be fully initialized
		auto pn = sst->pDist(-1);
		auto pdt = std::get<0>(pn); // note that these are unique positions
		auto unq = std::get<1>(pn);
		prbHist.push_back(pdt);
		unqHist.push_back(unq);
	}

	auto probIT = [this, prbHist, unqHist](unsigned int i, unsigned int t) {
		auto pdt = prbHist[t];
		auto unq = unqHist[t];
		auto sst = ((const SMPState*)(history[t]));
		double pr = sst->posProb(i, unq, pdt);
		return pr;
	};

	// TODO: displaying the probabilities of actors winning is a bit odd,
	// as we display the probability of their position winning. As multiple
	// actors often occupy the equivalent positions, this means the displayed probabilities
	// will often add up to more than 1.
	for (unsigned int i = 0; i < numAct; i++) {
		printf("%s , prob , ", actrs[i]->name.c_str());
		for (unsigned int t = 0; t < history.size(); t++) {
			printf("%.4f , ", probIT(i, t)); //  prbHist[t](i, 0),
		}
		cout << endl << flush;
	}
	return;
}

SMPModel * SMPModel::initModel(vector<string> aName, vector<string> aDesc, vector<string> dName,
                               const KMatrix & cap, // one row per actor
                               const KMatrix & pos, // one row per actor, one column per dimension
                               const KMatrix & sal, // one row per actor, one column per dimension
                               const KMatrix & accM,
                               uint64_t s, vector<bool> f)
{
    
  //  cout << "Num aName "<< aName.size() << endl;
  //  cout << "Num aDesc "<< aDesc.size() << endl;
  //  cout << "Num dName "<< dName.size() << endl;
  //  auto sfn = [](const string str, const KMatrix & m) { 
  //  cout << "Dim "<<str<<": "<<m.numR() << ", "<<m.numC() <<endl;
  //  m.mPrintf(" %.4f ");
  //  return;
  //  };
  //  sfn("cap", cap);
  //  sfn("pos", pos);
  //  sfn("sal", sal);
  //  sfn("accM", accM);
  //  cout << flush; 
    
    assert(f.size() == Model::NumSQLLogGrps + NumSQLLogGrps);
    SMPModel * sm0 = new SMPModel("", s, f); // JAH 20160711 added rng seed 20160730 JAH added sql flags
    SMPState * st0 = new SMPState(sm0);
  sm0->addState(st0);

    st0->step = [st0]() {
        return st0->stepBCN();
    };

    auto na = ((const unsigned int)(aName.size()));
    auto nd = ((const unsigned int)(dName.size()));


    for (auto dn : dName) {
        sm0->addDim(dn);
    }
  
    for (unsigned int i = 0; i < na; i++) {
        auto ai = new SMPActor(aName[i], aDesc[i]);
        ai->sCap = cap(i, 0);
        ai->vSal = KMatrix(nd, 1);
        auto vpi = new VctrPstn(nd, 1);
        for (unsigned int j = 0; j < nd; j++) {
            ai->vSal(j, 0) = sal(i, j);
            (*vpi)(j, 0) = pos(i, j);
        }
        sm0->addActor(ai);
        st0->addPstn(vpi);
    }

    st0->setAccomodate(accM);
    st0->idealsFromPstns();

    return sm0;
}

void SMPModel::setDBPath(std::string dbName)
{
    dbPath = dbName;
}

void SMPModel::csvReadExec(uint64_t seed, string inputCSV, vector<bool> f, string dbFilePath, vector<int> par) {
    SMPModel::setDBPath(dbFilePath);
    auto md0 = csvRead(inputCSV, seed, f);
    if(false==par.empty())
        SMPModel::updateModelParameters(md0,par);
    configExec(md0);
    delete md0;
    return;
}

void SMPModel::xmlReadExec(string inputXML, vector<bool> f, string dbFilePath) {
    SMPModel::setDBPath(dbFilePath);
    auto md0 = SMPModel::xmlRead(inputXML, f);
  cout << "Executing model" << endl << flush;
  configExec(md0);
    delete md0;
    return;
}

void SMPModel::configExec(SMPModel * md0)
{
    // setup the stopping criteria and lambda function
    const unsigned int minIter = 2;
    const unsigned int maxIter = 100;
    const double minDeltaRatio = 0.02;
    // suppose that, on a [0,100] scale, the first move was the most extreme possible,
    // i.e. 100 points. One fiftieth of that is just 2, which seems to about the limit
    // of what people consider significant.
    const double minSigDelta = 1E-4;
    // typical first shifts are on the order of numAct/10, so this is low
    // enough not to affect anything while guarding against the theoretical
    // possiblity of 0/0 errors
    md0->stop = [maxIter](unsigned int iter, const State * s) {
        return (maxIter <= iter);
    };
    md0->stop = smpStopFn(minIter, maxIter, minDeltaRatio, minSigDelta);

    // execute
    cout << "Starting model run" << endl << flush;
    md0->run();
    const unsigned int nState = md0->history.size();

    // log data, or not
    // JAH 20160731 added to either log all information tables or none
    // this takes care of info re. actors, dimensions, scenario, capabilities, and saliences
    if (md0->sqlFlags[0])
    {
        md0->LogInfoTables();
    }
    // JAH 20160802 added logging control flag for the last state
    // also added the sqlPosVote and sqlPosEquiv calls to get the final state
    if (md0->sqlFlags[1])
    {
        md0->sqlAUtil(nState - 1);
        md0->sqlPosProb(nState - 1);
        md0->sqlPosEquiv(nState - 1);
        md0->sqlPosVote(nState - 1);
    }

    cout << "Completed model run" << endl << endl;
    printf("There were %u states, with %i steps between them\n", nState, nState - 1);
    cout << "History of actor positions over time" << endl;
    md0->showVPHistory();

    return;
}

//Model Parameters
void SMPModel::updateModelParameters(SMPModel *md0, vector <int> parameters)
{
    md0->vpm= (VPModel)parameters.at(0); //victProbModel
    md0->pcem = (PCEModel)parameters.at(1); //pCEModel
    md0->stm = (StateTransMode)parameters.at(2); //stateTransitions
    md0->vrCltn = (VotingRule)parameters.at(3); //votingRule
    md0->bigRAdj = (BigRAdjust)parameters.at(4); //bigRAdjust
    md0->bigRRng = (BigRRange)parameters.at(5); //bigRRange
    md0->tpCommit = (ThirdPartyCommit)parameters.at(6); //thirdPartyCommit
    md0->ivBrgn = (InterVecBrgn)parameters.at(7); //interVecBrgn
    md0->brgnMod = (SMPBargnModel)parameters.at(8); //bargnModel
}

}; // end of namespace

// --------------------------------------------
// Copyright KAPSARC. Open source MIT License.
// --------------------------------------------
