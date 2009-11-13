
/*  CoinCbc.cpp  */


#include "CoinProblem.h"
#include "CoinResult.h"
#include "CoinSolver.h"

#include <cfloat>

#include "CoinHelperFunctions.hpp"
#include "CoinMessageHandler.hpp"

#include "ClpSimplex.hpp"
#include "ClpPrimalColumnSteepest.hpp"
#include "ClpDualRowSteepest.hpp"
#include "ClpEventHandler.hpp"

#include "OsiSolverInterface.hpp"
#include "OsiClpSolverInterface.hpp"

#include "CbcModel.hpp"
#include "CbcSolver.hpp"
#include "CbcEventHandler.hpp"
#include "CbcBranchActual.hpp"   // CbcSOS
#include "CbcBranchLotsize.hpp"  // CbcLotsize

#include "CglProbing.hpp"
#include "CglGomory.hpp"
#include "CglKnapsackCover.hpp"
#include "CglOddHole.hpp"
#include "CglClique.hpp"
#include "CglLiftAndProject.hpp"
#include "CglSimpleRounding.hpp"


#include "CoinCbc.h"

#define NEW_STYLE_CBCMAIN


/************************************************************************/
/*  Message Callback Handler                                            */
/************************************************************************/


class CBMessageHandler : public CoinMessageHandler {
public: 
	void setCallback(MSGLOGCALLBACK msgCallback);
	virtual int print();
private:
	MSGLOGCALLBACK msgCallback_;
};


void CBMessageHandler::setCallback(MSGLOGCALLBACK msgCallback)
{
  msgCallback_ = msgCallback;
}


int CBMessageHandler::print()
{
	msgCallback_(const_cast<char*>(messageBuffer()));
	return CoinMessageHandler::print();
}



/************************************************************************/
/*  Iteration Callback Handler                                          */
/************************************************************************/


class CBIterHandler : public ClpEventHandler {

public: 
   void setIterCallback(ITERCALLBACK iterCallback);

	virtual int event(Event whichEvent);
   
  /** Default constructor. */
  CBIterHandler();
  /// Constructor with pointer to model (redundant as setEventHandler does)
  CBIterHandler(ClpSimplex * model);
  /** Destructor */
  virtual ~CBIterHandler();
  /** The copy constructor. */
  CBIterHandler(const CBIterHandler & rhs);
  /// Assignment
  CBIterHandler& operator=(const CBIterHandler & rhs);
  /// Clone
  virtual ClpEventHandler * clone() const ;


private:
	ITERCALLBACK iterCallback_;
};


void CBIterHandler::setIterCallback(ITERCALLBACK iterCallback)
{
  iterCallback_ = iterCallback;
}



int CBIterHandler::event(Event whichEvent)
{
	int numIter;
	double objValue;
	int isPrimalFeasible;
	int isDualFeasible;
	double sumPrimalInfeas; 
	double sumDualInfeas;
	int cancelAsap;

	if ((whichEvent==endOfIteration)) {
		numIter = model_->getIterationCount();
		objValue = model_->getObjValue();
		sumDualInfeas = model_->sumDualInfeasibilities();
		sumPrimalInfeas = model_->sumPrimalInfeasibilities();
		isPrimalFeasible = model_->primalFeasible();
		isDualFeasible = model_->dualFeasible();
		cancelAsap = iterCallback_(numIter, objValue, isPrimalFeasible&&isDualFeasible, sumPrimalInfeas);
		if (cancelAsap) {
			return 5;
		}
	}
	return -1;
}


// Default Constructor 
CBIterHandler::CBIterHandler () : ClpEventHandler() {}

// Copy constructor 
CBIterHandler::CBIterHandler (const CBIterHandler & rhs) : ClpEventHandler(rhs) {}

// Constructor with pointer to model
CBIterHandler::CBIterHandler(ClpSimplex * model) : ClpEventHandler(model) {}

// Destructor 
CBIterHandler::~CBIterHandler () {}

// Assignment operator 
CBIterHandler & CBIterHandler::operator=(const CBIterHandler& rhs)
{
  if (this != &rhs) {
    ClpEventHandler::operator=(rhs);
  }
  return *this;
}

// Clone
ClpEventHandler * CBIterHandler::clone() const
{
	CBIterHandler * iterhandler;

   iterhandler = new CBIterHandler(*this);
	iterhandler->iterCallback_ = this->iterCallback_;
	return iterhandler;
}



/************************************************************************/
/*  MIP Node Callback Handler                                           */
/************************************************************************/


class CBNodeHandler : public CbcEventHandler {

public: 
   void setCallback(MIPNODECALLBACK mipNodeCallback);

	virtual CbcAction event(CbcEvent whichEvent);
   
  /** Default constructor. */
  CBNodeHandler();
  /// Constructor with pointer to model (redundant as setEventHandler does)
  //JPF CBNodeHandler(ClpSimplex * model);
  CBNodeHandler(CbcModel * model);
  

  /** Destructor */
  virtual ~CBNodeHandler();
  /** The copy constructor. */
  CBNodeHandler(const CBNodeHandler & rhs);
  /// Assignment
  CBNodeHandler& operator=(const CBNodeHandler & rhs);
  /// Clone
  virtual CbcEventHandler * clone() const ;


private:
	MIPNODECALLBACK mipNodeCallback_;
	int lastSolCount_;
};


void CBNodeHandler::setCallback(MIPNODECALLBACK mipNodeCallback)
{
	mipNodeCallback_ = mipNodeCallback;
	lastSolCount_ = 0;
}


CBNodeHandler::CbcAction CBNodeHandler::event(CbcEvent whichEvent)
{
	int numIter;
	int numNodes;
	double bestBound;
	double objValue;
	int solCount;
	int cancelAsap;

	if (whichEvent==node) {
		numIter = model_->getIterationCount();
		numNodes = model_->getNodeCount();
		objValue = model_->getObjValue();
		bestBound = model_->getBestPossibleObjValue();
		solCount = model_->getSolutionCount();
		cancelAsap = mipNodeCallback_(numIter, numNodes, bestBound, objValue, solCount != lastSolCount_);
		lastSolCount_ = solCount;
		if (cancelAsap) {
			return stop;
		}
	}
	return noAction;
}


// Default Constructor 
CBNodeHandler::CBNodeHandler () : CbcEventHandler() {}

// Copy constructor 
CBNodeHandler::CBNodeHandler (const CBNodeHandler & rhs) : CbcEventHandler(rhs) {}

// Constructor with pointer to model
//JPF: CBNodeHandler::CBNodeHandler(ClpSimplex * model) : CbcEventHandler(model) {}
CBNodeHandler::CBNodeHandler(CbcModel * model) : CbcEventHandler(model) {}

// Destructor 
CBNodeHandler::~CBNodeHandler () {}

// Assignment operator 
CBNodeHandler & CBNodeHandler::operator=(const CBNodeHandler& rhs)
{
  if (this != &rhs) {
    CbcEventHandler::operator=(rhs);
  }
  return *this;
}

// Clone
CbcEventHandler * CBNodeHandler::clone() const
{
	CBNodeHandler * nodehandler;

	nodehandler = new CBNodeHandler(*this);
	nodehandler->mipNodeCallback_ = this->mipNodeCallback_;
	nodehandler->lastSolCount_ = this->lastSolCount_;
	return nodehandler;
}


/************************************************************************/
/*  Coin Problem Info Structure                                         */
/************************************************************************/

typedef struct {
				ClpSimplex *clp;
				ClpSolve *clp_presolve;
				OsiClpSolverInterface *osi;
				CbcModel *cbc;

				CglProbing *probing;
				CglGomory *gomory;
				CglKnapsackCover *knapsack;
				CglOddHole *oddhole;
				CglClique *clique;
				CglLiftAndProject *liftpro;
				CglSimpleRounding *rounding;

				// callback handlers
				CBMessageHandler *msghandler;
				CBIterHandler *iterhandler;
				CBNodeHandler *nodehandler;

				} CBCINFO, *PCBC;


HCBC CbcCreateSolverObject(void)
{
	PCBC pCbc;

	pCbc = (PCBC)malloc(sizeof(CBCINFO));
	if (!pCbc) {
		return NULL;
	}
	memset(pCbc, 0, sizeof(CBCINFO));

	pCbc->clp = new ClpSimplex();
	pCbc->clp_presolve = new ClpSolve();
	pCbc->osi = new OsiClpSolverInterface(pCbc->clp);
	pCbc->cbc = NULL;  /* ERRORFIX 2/22/05: Crashes if not NULL when trying to set message handler */

	pCbc->msghandler = NULL;
	pCbc->iterhandler = NULL;
	pCbc->nodehandler = NULL;
	return (HCBC)pCbc;
}


void CbcClearSolverObject(HCBC hCbc)
{
	PCBC pCbc = (PCBC)hCbc;
	if (!pCbc) {
		return;
	}
	if (pCbc->clp) delete pCbc->clp;
	if (pCbc->clp_presolve) delete pCbc->clp_presolve;
	//if (pCbc->osi) delete pCbc->osi;
	if (pCbc->cbc) delete pCbc->cbc;

	if (pCbc->msghandler)	delete pCbc->msghandler;
	if (pCbc->iterhandler)	delete pCbc->iterhandler;
	if (pCbc->nodehandler)	delete pCbc->nodehandler;

	free(pCbc);
}

/************************************************************************/
/*  Callback Handling                                                   */
/************************************************************************/



int CbcSetMsgLogCallback(HCBC hCbc, MSGLOGCALLBACK MsgLogCallback)
{
	PCBC pCbc = (PCBC)hCbc;
	int LogLevel;

	if (!MsgLogCallback) {
		return CBC_CALL_FAILED;
	}
	//pCbc->MsgLogCallback = MsgLogCallback;
	delete pCbc->msghandler;
	pCbc->msghandler = new CBMessageHandler();
	pCbc->msghandler->setCallback(MsgLogCallback);
	LogLevel = CbcGetIntOption(COIN_INT_LOGLEVEL);
	pCbc->msghandler->setLogLevel(LogLevel);
	if (pCbc->clp) pCbc->clp->passInMessageHandler(pCbc->msghandler);
	if (pCbc->cbc) pCbc->cbc->passInMessageHandler(pCbc->msghandler);
	if (pCbc->osi) pCbc->osi->passInMessageHandler(pCbc->msghandler);
	return CBC_CALL_SUCCESS;
}



int CbcSetIterCallback(HCBC hCbc, ITERCALLBACK IterCallback)
{
	PCBC pCbc = (PCBC)hCbc;

	if (!IterCallback) {
		return CBC_CALL_FAILED;
	}
	//pCbc->IterCallback = IterCallback;
	delete pCbc->iterhandler;
	pCbc->iterhandler = new CBIterHandler(pCbc->clp);
	pCbc->iterhandler->setIterCallback(IterCallback);
	if (pCbc->clp) pCbc->clp->passInEventHandler(pCbc->iterhandler);
	return CBC_CALL_SUCCESS;
}


int CbcSetMipNodeCallback(HCBC hCbc, MIPNODECALLBACK MipNodeCallback)
{
	PCBC pCbc = (PCBC)hCbc;

	if (!MipNodeCallback) {
		return CBC_CALL_FAILED;
	}
	//pCbc->MipNodeCallback = MipNodeCallback;
	delete pCbc->nodehandler;
	pCbc->nodehandler = new CBNodeHandler(pCbc->cbc);
	pCbc->nodehandler->setCallback(MipNodeCallback);
	if (pCbc->cbc) pCbc->cbc->passInEventHandler(pCbc->nodehandler);
	return CBC_CALL_SUCCESS;
}


int CbcSetAllCallbacks(HCBC hCbc, PSOLVER pSolver)
{
	CbcSetMsgLogCallback(hCbc, pSolver->MsgLogCallback);
	CbcSetIterCallback(hCbc, pSolver->IterCallback);
	CbcSetMipNodeCallback(hCbc, pSolver->MipNodeCallback);
	return CBC_CALL_SUCCESS;
}



/************************************************************************/
/*  Option Setting                                                      */
/************************************************************************/


int CbcSetClpOptions(HCBC hCbc)
{
	PCBC pCbc = (PCBC)hCbc;
	ClpSolve::SolveType method;
	ClpSolve::PresolveType presolve;

	/* check if it has been changed, leave alone otherwise */
	if (CbcGetOptionChanged(COIN_INT_SCALING))			pCbc->clp->scaling(CbcGetIntOption(COIN_INT_SCALING));
	if (CbcGetOptionChanged(COIN_INT_PERTURBATION))		pCbc->clp->setPerturbation(CbcGetIntOption(COIN_INT_PERTURBATION));

	if (CbcGetOptionChanged(COIN_REAL_MAXSECONDS))		pCbc->clp->setMaximumSeconds(CbcGetRealOption(COIN_REAL_MAXSECONDS));
	if (CbcGetOptionChanged(COIN_INT_MAXITER))			pCbc->clp->setMaximumIterations(CbcGetIntOption(COIN_INT_MAXITER));

	if (CbcGetOptionChanged(COIN_REAL_PRIMALOBJLIM))	pCbc->clp->setPrimalObjectiveLimit(CbcGetRealOption(COIN_REAL_PRIMALOBJLIM));
	if (CbcGetOptionChanged(COIN_REAL_DUALOBJLIM))		pCbc->clp->setDualObjectiveLimit(CbcGetRealOption(COIN_REAL_DUALOBJLIM));
	if (CbcGetOptionChanged(COIN_REAL_PRIMALOBJTOL))	pCbc->clp->setPrimalTolerance(CbcGetRealOption(COIN_REAL_PRIMALOBJTOL));
	if (CbcGetOptionChanged(COIN_REAL_DUALOBJTOL))		pCbc->clp->setDualTolerance(CbcGetRealOption(COIN_REAL_DUALOBJTOL));

	if (CbcGetOptionChanged(COIN_INT_PRIMALPIVOTALG)) {
		ClpPrimalColumnSteepest primalSteepest(CbcGetIntOption(COIN_INT_PRIMALPIVOTALG));
		pCbc->clp->setPrimalColumnPivotAlgorithm(primalSteepest);
	}

	if (CbcGetOptionChanged(COIN_INT_DUALPIVOTALG)) {
		ClpDualRowSteepest dualSteepest(CbcGetIntOption(COIN_INT_DUALPIVOTALG));
		pCbc->clp->setDualRowPivotAlgorithm(dualSteepest);
	}

	if (CbcGetOptionChanged(COIN_INT_CRASHIND)) { 
		if (CbcGetIntOption(COIN_INT_CRASHIND)) {
			pCbc->clp->crash(CbcGetRealOption(COIN_REAL_CRASHGAP),
								CbcGetIntOption(COIN_INT_CRASHPIVOT));
		}
	}

	if (CbcGetOptionChanged(COIN_INT_SOLVEMETHOD)) {
		switch (CbcGetIntOption(COIN_INT_SOLVEMETHOD)) {
			case 0: method = ClpSolve::useDual;				break;
			case 1: method = ClpSolve::usePrimal;			break;
			case 2: method = ClpSolve::usePrimalorSprint;	break;
			case 3: method = ClpSolve::useBarrier;			break;
			case 4: method = ClpSolve::useBarrierNoCross;	break;
			case 5: method = ClpSolve::automatic;			break;
			default: method = ClpSolve::usePrimal;
		}
		pCbc->clp->setSolveType(method);   //ClpSolve::usePrimal
	}

	if (CbcGetOptionChanged(COIN_INT_PRESOLVETYPE)) {   
		switch (CbcGetIntOption(COIN_INT_PRESOLVETYPE)) {
			case 0: presolve = ClpSolve::presolveOn;		 break;
			case 1: presolve = ClpSolve::presolveOff;		 break;
			case 2: presolve = ClpSolve::presolveNumber;	 break;
			default: presolve = ClpSolve::presolveOn;
		}
		pCbc->clp_presolve->setPresolveType(presolve);   //ClpSolve::presolveOn
	}
	return 1;
}


int CbcSetCbcOptions(HCBC hCbc)
{
	PCBC pCbc = (PCBC)hCbc;

	if (!pCbc->cbc) {
		return 0;
	}
	if (CbcGetOptionChanged(COIN_INT_MIPMAXNODES))		pCbc->cbc->setMaximumNodes(CbcGetIntOption(COIN_INT_MIPMAXNODES));
	if (CbcGetOptionChanged(COIN_INT_MIPMAXSOL))		pCbc->cbc->setMaximumSolutions(CbcGetIntOption(COIN_INT_MIPMAXSOL));
	if (CbcGetOptionChanged(COIN_REAL_MIPMAXSEC))		pCbc->cbc->setDblParam(CbcModel::CbcMaximumSeconds,CbcGetRealOption(COIN_REAL_MIPMAXSEC));

	if (CbcGetOptionChanged(COIN_INT_MIPFATHOMDISC))	pCbc->cbc->setIntParam(CbcModel::CbcFathomDiscipline,CbcGetIntOption(COIN_INT_MIPFATHOMDISC));

	// JPF commented: pCbc->cbc->setHotstartStrategy(CbcGetIntOption(COIN_INT_MIPHOTSTART));
	//		pCbc->cbc->setForcePriority(CbcGetIntOption(COIN_INT_MIPFORCEPRIOR));

	if (CbcGetOptionChanged(COIN_INT_MIPMINIMUMDROP))	pCbc->cbc->setMinimumDrop(CbcGetIntOption(COIN_INT_MIPMINIMUMDROP));
	if (CbcGetOptionChanged(COIN_INT_MIPMAXPASSROOT))	pCbc->cbc->setMaximumCutPassesAtRoot(CbcGetIntOption(COIN_INT_MIPMAXPASSROOT));
	if (CbcGetOptionChanged(COIN_INT_MIPMAXCUTPASS))	pCbc->cbc->setMaximumCutPasses(CbcGetIntOption(COIN_INT_MIPMAXCUTPASS));
	if (CbcGetOptionChanged(COIN_INT_MIPSTRONGBRANCH))	pCbc->cbc->setNumberStrong(CbcGetIntOption(COIN_INT_MIPSTRONGBRANCH));
	if (CbcGetOptionChanged(COIN_INT_MIPSCANGLOBCUTS))	pCbc->cbc->setHowOftenGlobalScan(CbcGetIntOption(COIN_INT_MIPSCANGLOBCUTS));

	if (CbcGetOptionChanged(COIN_REAL_MIPINTTOL))		pCbc->cbc->setIntegerTolerance(CbcGetRealOption(COIN_REAL_MIPINTTOL));
	if (CbcGetOptionChanged(COIN_REAL_MIPINFWEIGHT))	pCbc->cbc->setInfeasibilityWeight(CbcGetRealOption(COIN_REAL_MIPINFWEIGHT));
	if (CbcGetOptionChanged(COIN_REAL_MIPCUTOFF))		pCbc->cbc->setDblParam(CbcModel::CbcCutoffIncrement,CbcGetRealOption(COIN_REAL_MIPCUTOFF));
	if (CbcGetOptionChanged(COIN_REAL_MIPABSGAP))		pCbc->cbc->setAllowableGap(CbcGetRealOption(COIN_REAL_MIPABSGAP));
	if (CbcGetOptionChanged(COIN_REAL_MIPFRACGAP))		pCbc->cbc->setAllowableFractionGap(CbcGetRealOption(COIN_REAL_MIPFRACGAP));
	return 1;
}


int CbcSetCglOptions(HCBC hCbc)
{
	PCBC pCbc = (PCBC)hCbc;

	/* see CbcModel.hpp has commments on calling cuts */
	if (CbcGetIntOption(COIN_INT_MIPCUT_PROBING)) {
		pCbc->probing = new CglProbing();
		if (CbcGetOptionChanged(COIN_INT_MIPPROBE_MODE))	pCbc->probing->setMode(CbcGetIntOption(COIN_INT_MIPPROBE_MODE));
		if (CbcGetOptionChanged(COIN_INT_MIPPROBE_USEOBJ))	pCbc->probing->setUsingObjective(CbcGetIntOption(COIN_INT_MIPPROBE_USEOBJ) ? true : false);
		if (CbcGetOptionChanged(COIN_INT_MIPPROBE_MAXPASS))	pCbc->probing->setMaxPass(CbcGetIntOption(COIN_INT_MIPPROBE_MAXPASS));
		if (CbcGetOptionChanged(COIN_INT_MIPPROBE_MAXPROBE))	pCbc->probing->setMaxProbe(CbcGetIntOption(COIN_INT_MIPPROBE_MAXPROBE));
		if (CbcGetOptionChanged(COIN_INT_MIPPROBE_MAXLOOK))	pCbc->probing->setMaxLook(CbcGetIntOption(COIN_INT_MIPPROBE_MAXLOOK));
		if (CbcGetOptionChanged(COIN_INT_MIPPROBE_ROWCUTS))	pCbc->probing->setRowCuts(CbcGetIntOption(COIN_INT_MIPPROBE_ROWCUTS));
		pCbc->cbc->addCutGenerator(pCbc->probing,CbcGetIntOption(COIN_INT_MIPPROBE_FREQ),"Probing");
	}

	if (CbcGetIntOption(COIN_INT_MIPCUT_GOMORY)) {
		pCbc->gomory = new CglGomory();
		if (CbcGetOptionChanged(COIN_INT_MIPGOMORY_LIMIT))	pCbc->gomory->setLimit(CbcGetIntOption(COIN_INT_MIPGOMORY_LIMIT));
		if (CbcGetOptionChanged(COIN_REAL_MIPGOMORY_AWAY))	pCbc->gomory->setAway(CbcGetRealOption(COIN_REAL_MIPGOMORY_AWAY));
		pCbc->cbc->addCutGenerator(pCbc->gomory,CbcGetIntOption(COIN_INT_MIPGOMORY_FREQ),"Gomory");
	}

	if (CbcGetIntOption(COIN_INT_MIPCUT_KNAPSACK)) {
		pCbc->knapsack = new CglKnapsackCover();
		if (CbcGetOptionChanged(COIN_INT_MIPKNAPSACK_MAXIN))	pCbc->knapsack->setMaxInKnapsack(CbcGetIntOption(COIN_INT_MIPKNAPSACK_MAXIN));
		pCbc->cbc->addCutGenerator(pCbc->knapsack,CbcGetIntOption(COIN_INT_MIPKNAPSACK_FREQ),"Knapsack");
	}

	if (CbcGetIntOption(COIN_INT_MIPCUT_ODDHOLE)) {
		pCbc->oddhole= new CglOddHole();
		if (CbcGetOptionChanged(COIN_REAL_MIPODDHOLE_MINVIOL))	pCbc->oddhole->setMinimumViolation(CbcGetRealOption(COIN_REAL_MIPODDHOLE_MINVIOL));
		if (CbcGetOptionChanged(COIN_REAL_MIPODDHOLE_MINVIOLPER))	pCbc->oddhole->setMinimumViolationPer(CbcGetRealOption(COIN_REAL_MIPODDHOLE_MINVIOLPER));
		if (CbcGetOptionChanged(COIN_INT_MIPODDHOLE_MAXENTRIES))	pCbc->oddhole->setMaximumEntries(CbcGetIntOption(COIN_INT_MIPODDHOLE_MAXENTRIES));
		pCbc->cbc->addCutGenerator(pCbc->oddhole,CbcGetIntOption(COIN_INT_MIPODDHOLE_FREQ),"OddHole");
	}

	if (CbcGetIntOption(COIN_INT_MIPCUT_CLIQUE)) {
		pCbc->clique= new CglClique(CbcGetIntOption(COIN_INT_MIPCLIQUE_PACKING) ? true : false);
		if (CbcGetOptionChanged(COIN_INT_MIPCLIQUE_STAR))		pCbc->clique->setDoStarClique(CbcGetIntOption(COIN_INT_MIPCLIQUE_STAR) ? true : false);
		if (CbcGetOptionChanged(COIN_INT_MIPCLIQUE_STARMETHOD))	pCbc->clique->setStarCliqueNextNodeMethod((CglClique::scl_next_node_method)CbcGetIntOption(COIN_INT_MIPCLIQUE_STARMETHOD));
		if (CbcGetOptionChanged(COIN_INT_MIPCLIQUE_STARMAXLEN))	pCbc->clique->setStarCliqueCandidateLengthThreshold(CbcGetIntOption(COIN_INT_MIPCLIQUE_STARMAXLEN));
		if (CbcGetOptionChanged(COIN_INT_MIPCLIQUE_STARREPORT))	pCbc->clique->setStarCliqueReport(CbcGetIntOption(COIN_INT_MIPCLIQUE_STARREPORT) ? true : false);
		if (CbcGetOptionChanged(COIN_INT_MIPCLIQUE_ROW))		pCbc->clique->setDoRowClique(CbcGetIntOption(COIN_INT_MIPCLIQUE_ROW) ? true : false);
		if (CbcGetOptionChanged(COIN_INT_MIPCLIQUE_ROWMAXLEN))	pCbc->clique->setRowCliqueCandidateLengthThreshold(CbcGetIntOption(COIN_INT_MIPCLIQUE_ROWMAXLEN));
		if (CbcGetOptionChanged(COIN_INT_MIPCLIQUE_ROWREPORT))	pCbc->clique->setRowCliqueReport(CbcGetIntOption(COIN_INT_MIPCLIQUE_ROWREPORT) ? true : false);
		if (CbcGetOptionChanged(COIN_REAL_MIPCLIQUE_MINVIOL))	pCbc->clique->setMinViolation(CbcGetRealOption(COIN_REAL_MIPCLIQUE_MINVIOL));
		pCbc->cbc->addCutGenerator(pCbc->clique,CbcGetIntOption(COIN_INT_MIPCLIQUE_FREQ),"Clique");
	}

	if (CbcGetIntOption(COIN_INT_MIPCUT_LIFTPROJECT)) {
		pCbc->liftpro = new CglLiftAndProject();
		if (CbcGetOptionChanged(COIN_INT_MIPLIFTPRO_BETAONE))	pCbc->liftpro->setBeta(CbcGetIntOption(COIN_INT_MIPLIFTPRO_BETAONE) ? 1 : -1);
		pCbc->cbc->addCutGenerator(pCbc->liftpro,CbcGetIntOption(COIN_INT_MIPLIFTPRO_FREQ),"LiftProject");
	}

	if (CbcGetIntOption(COIN_INT_MIPCUT_SIMPROUND)) {
		pCbc->rounding = new CglSimpleRounding();
		pCbc->cbc->addCutGenerator(pCbc->rounding,CbcGetIntOption(COIN_INT_MIPSIMPROUND_FREQ),"Rounding");
	}
	return 1;
}


/************************************************************************/
/*  Setup CBC Objects                                                  */
/************************************************************************/


void CbcCopyNamesList(HCBC hCbc, PPROBLEM pProblem)
{
	int i;
	PCBC pCbc = (PCBC)hCbc;

	/* Load names */
	if (pProblem->RowNamesList || pProblem->ColNamesList) {
		std::vector<std::string> rowNamesVect;
		std::vector<std::string> colNamesVect;
		rowNamesVect.reserve(pProblem->RowCount);
		colNamesVect.reserve(pProblem->ColCount);
		if (pProblem->RowNamesList) {
			for (i = 0; i < pProblem->RowCount; i++) {
				rowNamesVect.push_back(pProblem->RowNamesList[i]);
			}
		}
		if (pProblem->ColNamesList) {
			for (i = 0; i < pProblem->ColCount; i++) {
				colNamesVect.push_back(pProblem->ColNamesList[i]);
			}
		}
		pCbc->clp->copyNames(rowNamesVect, colNamesVect);
	}
}


int CbcSetColumnIntegers(HCBC hCbc, PPROBLEM pProblem)
{
	int i;
	PCBC pCbc = (PCBC)hCbc;

	if (pProblem->numInts == 0) {
		return CBC_CALL_FAILED;
	}
	for (i = 0; i < pProblem->ColCount; i++) {
		if (pProblem->IsInt[i]) {
			pCbc->cbc->solver()->setInteger(i);
			pCbc->osi->setInteger(i);
		}
	}
	return CBC_CALL_SUCCESS;
}


int CbcAddPriorObjects(HCBC hCbc, PPROBLEM pProblem)
{
	int *priorVar;
	int *priorCbc;
	int i,k;
	PCBC pCbc = (PCBC)hCbc;

	if ((pProblem->PriorCount == 0) || (pProblem->numInts == 0)) {
		return CBC_CALL_FAILED;
	}
	priorVar = (int *)malloc(pProblem->ColCount * sizeof(int));
	if (!priorVar) {
		return CBC_CALL_FAILED;
	}
	//reset the priorVar
	for (i = 0; i < pProblem->ColCount; i++) {
		priorVar[i] = 1000;
	}
	for (i = 0; i < pProblem->PriorCount; i++) {
		if ((pProblem->PriorIndex[i] < 0) || 
			(pProblem->PriorIndex[i] >= pProblem->ColCount)) {
			free(priorVar);
			return CBC_CALL_FAILED;
		}
		priorVar[pProblem->PriorIndex[i]] = pProblem->PriorValues[i];
	}
	//Create an array to give to cbc
	priorCbc = (int *)malloc(pProblem->numInts * sizeof(int));
	if (!priorCbc) {
		free(priorVar);
		return CBC_CALL_FAILED;
	}
	k = 0;
	for (i = 0; i < pProblem->ColCount; i++) {
		if (pProblem->IsInt[i]) {
			priorCbc[k++] = priorVar[i];
		}
	}
	pCbc->cbc->passInPriorities(priorCbc, false);
	free(priorCbc);
	free(priorVar);
	return CBC_CALL_SUCCESS;
}


int CbcAddSosObjects(HCBC hCbc, PPROBLEM pProblem)
{
	int sos, count;
	int* which;
	int type;
	PCBC pCbc = (PCBC)hCbc;

	if (pProblem->SosCount == 0) {
		return CBC_CALL_FAILED;
	}
	for (sos = 0; sos < pProblem->SosCount; sos++) {
		count = pProblem->SosBegin[sos+1] - pProblem->SosBegin[sos];
		which = &pProblem->SosIndex[pProblem->SosBegin[sos]];
		type = pProblem->SosType[sos];
		CbcObject *sosObject = new CbcSOS(pCbc->cbc, count, which, NULL, 0, type);
		pCbc->cbc->addObjects(1, &sosObject);
		delete sosObject;
	}
	return CBC_CALL_SUCCESS;
}


int CbcAddSemiContObjects(HCBC hCbc, PPROBLEM pProblem)
{
	double points[4];
	int i, semicol;
	PCBC pCbc = (PCBC)hCbc;

	if (pProblem->SemiCount == 0) {
		return CBC_CALL_FAILED;
	}
	points[0] = 0.0;
	points[1] = 0.0;
	for (i = 0; i < pProblem->SemiCount; i++ ) {
		semicol = pProblem->SemiIndex[i];
		points[2] = pProblem->LowerBounds[semicol];
		points[3] = pProblem->UpperBounds[semicol];
		CbcObject *semiObject = new CbcLotsize(pCbc->cbc, semicol, 2, points, true);
		pCbc->cbc->addObjects(1, &semiObject);
		delete semiObject;
	}
	return CBC_CALL_SUCCESS;
}



int CbcLoadAllSolverObjects(HCBC hCbc, PPROBLEM pProblem)
{
	int i, col;
	int result;
	PCBC pCbc = (PCBC)hCbc;

	if (pProblem->SemiCount > 0) { 
		memcpy(pProblem->SemiLower, pProblem->LowerBounds, pProblem->ColCount * sizeof(double));
		for (i = 0; i < pProblem->SemiCount; i++) {
			col = pProblem->SemiIndex[i];
			pProblem->LowerBounds[col] = 0.0;
		}
	}
	pCbc->clp->setOptimizationDirection(pProblem->ObjectSense);
	pCbc->clp->loadProblem(pProblem->ColCount, pProblem->RowCount, 
							pProblem->MatrixBegin, pProblem->MatrixIndex, pProblem->MatrixValues,
							pProblem->LowerBounds, pProblem->UpperBounds, pProblem->ObjectCoeffs, 
							pProblem->RowLower, pProblem->RowUpper);
	if (pProblem->SemiCount > 0) { 
		memcpy(pProblem->LowerBounds, pProblem->SemiLower, pProblem->ColCount * sizeof(double));
	}
	CbcCopyNamesList(hCbc, pProblem);
	if (pProblem->SolveAsMIP) {
		if (!pCbc->cbc) {
			pCbc->cbc = new CbcModel(*pCbc->osi);
		}
		result = CbcSetColumnIntegers(hCbc, pProblem);
		result = CbcAddPriorObjects(hCbc, pProblem);
		result = CbcAddSosObjects(hCbc, pProblem);
		result = CbcAddSemiContObjects(hCbc, pProblem);
	}
	return CBC_CALL_SUCCESS;
}



extern int CbcOrClpRead_mode;

int CbcSolveProblem(HCBC hCbc, PPROBLEM pProblem, int Method)
{
	PCBC pCbc = (PCBC)hCbc;

	if (!pProblem->SolveAsMIP) {
		CbcSetClpOptions(hCbc);
		if (CbcGetOptionChanged(COIN_INT_PRESOLVETYPE))			
			pCbc->clp->initialSolve(*pCbc->clp_presolve);
		else {
			pCbc->clp->initialSolve();
		}
		}
	else {
#ifdef NEW_STYLE_CBCMAIN
		if (CbcGetIntOption(COIN_INT_MIPUSECBCMAIN)) {
			CbcMain0(*pCbc->cbc);
			CbcSetClpOptions(hCbc);
			CbcSetCbcOptions(hCbc);
			//CbcSetCglOptions(hProb);  BK: CbcMain1 will call the Cgl's automatically
			CbcOrClpRead_mode = 1;  // BK: Fix bug in CbcMain1, CbcOrClpRead_mode not initialized  (CpcSolver.cpp, stable 2.2)
			const int argc = 3;
			const char* argv[] = {"CoinMP", "-solve", "-quit"};
			CbcMain1(argc,argv,*pCbc->cbc);
			}
		else 
#endif
		{
			CbcSetClpOptions(hCbc);
			CbcSetCbcOptions(hCbc);
			CbcSetCglOptions(hCbc);

			pCbc->cbc->initialSolve();
			pCbc->cbc->branchAndBound();
		}
	}
	return CBC_CALL_SUCCESS;
}


int CbcRetrieveSolutionResults(HCBC hCbc, PPROBLEM pProblem, PRESULT pResult)
{
	PCBC pCbc = (PCBC)hCbc;
	const double* columnPrimal;
	const double* columnDual;
	const double* rowPrimal;
	const double* rowDual;

	if (!pProblem->SolveAsMIP) {
		pResult->SolutionStatus = pCbc->clp->status();
		pResult->ObjectValue    = pCbc->clp->objectiveValue();
		pResult->MipBestBound   = 0.0;
		pResult->IterCount      = pCbc->clp->numberIterations();
		pResult->MipNodeCount	  = 0;
		}
	else {
		pResult->SolutionStatus = pCbc->cbc->status();
		pResult->ObjectValue    = pCbc->cbc->getObjValue();
		pResult->MipBestBound   = pCbc->cbc->getBestPossibleObjValue();
		pResult->IterCount      = pCbc->cbc->getIterationCount();
		pResult->MipNodeCount   = pCbc->cbc->getNodeCount();
	}

	switch (pResult->SolutionStatus) {
		case 0:	strcpy(pResult->SolutionText, "Optimal solution found");		break;
		case 1:	strcpy(pResult->SolutionText, "Problem primal infeasible");	break;
		case 2:	strcpy(pResult->SolutionText, "Problem dual infeasible");		break;
		case 3:	strcpy(pResult->SolutionText, "Stopped on iterations");			break;
		case 4: strcpy(pResult->SolutionText, "Stopped due to errors");			break;
		case 5: strcpy(pResult->SolutionText, "Stopped by user");		break;
		default: 
			sprintf(pResult->SolutionText, "Unknown CBC solution status (%d)", pResult->SolutionStatus);
			break;
	}

	if (!pProblem->SolveAsMIP) {
		columnPrimal = pCbc->clp->primalColumnSolution();
		columnDual = pCbc->clp->dualColumnSolution();
		rowPrimal = pCbc->clp->primalRowSolution();
		rowDual = pCbc->clp->dualRowSolution();
		pResult->ColActivity = (double*) malloc(pProblem->ColCount * sizeof(double));
		pResult->ReducedCost = (double*) malloc(pProblem->ColCount * sizeof(double));
		pResult->SlackValues = (double*) malloc(pProblem->RowCount * sizeof(double));
		pResult->ShadowPrice = (double*) malloc(pProblem->RowCount * sizeof(double));
		if (!pResult->ColActivity ||
			!pResult->ReducedCost || 
			!pResult->SlackValues || 
			!pResult->ShadowPrice) {
			return CBC_CALL_FAILED;
		}
		memcpy(pResult->ColActivity, columnPrimal, pProblem->ColCount * sizeof(double));
		memcpy(pResult->ReducedCost, columnDual, pProblem->ColCount * sizeof(double));
		memcpy(pResult->SlackValues, rowPrimal, pProblem->RowCount * sizeof(double));
		memcpy(pResult->ShadowPrice, rowDual, pProblem->RowCount * sizeof(double));
		}
	else {
		columnPrimal = pCbc->cbc->solver()->getColSolution();
		pResult->ColActivity = (double*) malloc(pProblem->ColCount * sizeof(double));
		if (!pResult->ColActivity) {
			return CBC_CALL_FAILED;
		}
		memcpy(pResult->ColActivity, columnPrimal, pProblem->ColCount * sizeof(double));
	}
	return CBC_CALL_SUCCESS;
}



int CbcOptimizeProblem(PPROBLEM pProblem, PRESULT pResult, PSOLVER pSolver, int Method)
{		
	HCBC hCbc;
	int result;

	hCbc = CbcCreateSolverObject();
	result = CbcLoadAllSolverObjects(hCbc, pProblem);
	result = CbcSetAllCallbacks(hCbc, pSolver);
	result = CbcSolveProblem(hCbc, pProblem, Method);
	result = CbcRetrieveSolutionResults(hCbc, pProblem, pResult);
	CbcClearSolverObject(hCbc);
	return CBC_CALL_SUCCESS;
}


int CbcReadMpsFile(PPROBLEM pProblem, const char *ReadFilename)
{
	PCBC pCbc;
	HCBC hCbc;

	hCbc = CbcCreateSolverObject();
	pCbc = (PCBC)hCbc;
	pCbc->clp->readMps(ReadFilename, 1, 0); 
	CbcClearSolverObject(hCbc);
	return CBC_CALL_SUCCESS;
}


int CbcWriteMpsFile(PPROBLEM pProblem, const char *WriteFilename)
{
	int result;
	PCBC pCbc;
	HCBC hCbc;

	hCbc = CbcCreateSolverObject();
	pCbc = (PCBC)hCbc;
	result = CbcLoadAllSolverObjects(hCbc, pProblem);
	pCbc->osi->writeMps(WriteFilename); 
	//result = CbcClearAllSolverObjects(hCbc);
	CbcClearSolverObject(hCbc);
	return CBC_CALL_SUCCESS;
}



/************************************************************************/
/*  Option Table                                                        */
/************************************************************************/

#define OPTIONCOUNT    68


SOLVOPTINFO OptionTable[OPTIONCOUNT] = {

	{"SolveMethod",            "SolveMethod",  GRP_SIMPLEX,        0,        0,      0,       5,  OPT_LIST,   0,   COIN_INT_SOLVEMETHOD},
      /*enum SolveType { useDual=0, usePrimal, usePrimalorSprint, useBarrier, useBarrierNoCross, automatic*/
	{"PresolveType",           "Presolve",     GRP_SIMPLEX,        0,        0,      0,       2,  OPT_LIST,   0,   COIN_INT_PRESOLVETYPE},
      /*enum PresolveType { presolveOn=0, presolveOff, presolveNumber }; */
	{"Scaling",                "Scaling",      GRP_SIMPLEX,        3,        3,      0,       4,  OPT_LIST,   0,   COIN_INT_SCALING}, 
      /* Sets or unsets scaling, 0 -off, 1 equilibrium, 2 geometric, 3, auto, 4 dynamic(later) */
	{"Perturbation",           "Perturb",      GRP_SIMPLEX,      100,      100,      0,     100,  OPT_INT,    0,   COIN_INT_PERTURBATION}, 
      /* 0 - Off, 50 - On, 100 - Only if clp stalls */
	{"PrimalColPivotAlg",      "PrimPivAlg",   GRP_SIMPLEX,        1,        1,      0,       4,  OPT_LIST,   0,   COIN_INT_PRIMALPIVOTALG}, 
      /*0 is exact devex, 1 full steepest, 2 is partial exact devex
        3 switches between 0 and 2 depending on factorization
        4 starts as partial dantzig/devex but then may switch between 0 and 2.*/
	{"DualRowPivotAlg",        "DualPivAlg",   GRP_SIMPLEX,        1,        1,      0,       3,  OPT_LIST,   0,   COIN_INT_DUALPIVOTALG}, 
      /*0 is uninitialized, 1 full, 2 is partial uninitialized,
        3 starts as 2 but may switch to 1.*/
	{"LogLevel",               "LogLevel",     GRP_LOGFILE,        1,        1,      0,       4,  OPT_LIST,   0,   COIN_INT_LOGLEVEL}, 
	{"MaxIterations",          "MaxIter",      GRP_LIMITS,  99999999, 99999999,      0,  MAXINT,  OPT_INT,    0,   COIN_INT_MAXITER}, 
	{"MaxSeconds",             "MaxSeconds",   GRP_LIMITS,      -1.0,     -1.0,   -1.0, MAXREAL,  OPT_REAL,   0,   COIN_REAL_MAXSECONDS}, 

	{"CrashInd",               "CrashInd",     GRP_SIMPLEX,        0,        0,      0,       1,  OPT_ONOFF,  0,   COIN_INT_CRASHIND}, 
	{"CrashPivot",             "CrashPivot",   GRP_SIMPLEX,        0,        0,     -1,       2,  OPT_LIST,   0,   COIN_INT_CRASHPIVOT}, 
	{"CrashGap",               "CrashGap",     GRP_SIMPLEX,   1000.0,   1000.0,    0.0, MAXREAL,  OPT_REAL,   0,   COIN_REAL_CRASHGAP}, 
	{"PrimalObjectLimit",      "PrimalObjLim", GRP_SIMPLEX,  MAXREAL,  MAXREAL,    0.0, MAXREAL,  OPT_REAL,   0,   COIN_REAL_PRIMALOBJLIM}, 
	{"DualObjectLimit",        "DualObjLim",   GRP_SIMPLEX,  MAXREAL,  MAXREAL,    0.0, MAXREAL,  OPT_REAL,   0,   COIN_REAL_DUALOBJLIM}, 
	{"PrimalTolerance",        "PrimalTol",    GRP_SIMPLEX,     1e-7,     1e-7,    0.0, MAXREAL,  OPT_REAL,   0,   COIN_REAL_PRIMALOBJTOL}, 
	{"DualTolerance",          "DualTol",      GRP_SIMPLEX,     1e-7,     1e-7,    0.0, MAXREAL,  OPT_REAL,   0,   COIN_REAL_DUALOBJTOL}, 

	{"MipMaxNodes",            "MipMaxNodes",  GRP_LIMITS,   9999999,  9999999,      0,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPMAXNODES}, 
	{"MipMaxSolutions",        "MipMaxSol",    GRP_LIMITS,   9999999,  9999999,      0,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPMAXSOL}, 
	{"MipMaxSeconds",          "MipMaxSec",    GRP_LIMITS,     1e100,    1e100,    0.0, MAXREAL,  OPT_REAL,   0,   COIN_REAL_MIPMAXSEC}, 

	{"MipFathomDiscipline",    "FathomDisc",   GRP_MIPSTRAT,       0,        0,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPFATHOMDISC}, 
	{"MipHotStart",            "HotStart",     GRP_MIPSTRAT,       0,        0,      0,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPHOTSTART},
//	{"MipForcePriority",       "ForcePrior",   GRP_MIPSTRAT,      -1,       -1,-MAXINT,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPFORCEPRIOR},
	{"MipMinimumDrop",         "MinimumDrop",  GRP_MIPSTRAT,    1e-4,     1e-4,-MAXINT,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPMINIMUMDROP},
	{"MipMaxCutPasses",        "MaxCutPass",   GRP_MIPSTRAT,       2,        2,-MAXINT,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPMAXCUTPASS},
	{"MipMaxCutPassAtRoot",    "MaxPassRoot",  GRP_MIPSTRAT,      50,       50,-MAXINT,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPMAXPASSROOT},
	{"MipStrongBranching",     "StrongBranch", GRP_MIPSTRAT,       5,        5,      0,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPSTRONGBRANCH},
	{"MipScanGlobalCuts",      "ScanGlobCuts", GRP_MIPSTRAT,       1,        1,      0,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPSCANGLOBCUTS},

	{"MipIntegerTolerance",    "MipIntTol",    GRP_MIPTOL,      1e-6,     1e-6,    0.0,     1.0,  OPT_REAL,   0,   COIN_REAL_MIPINTTOL}, 
	{"MipInfeasWeight",        "MipInfWeight", GRP_MIPTOL,       0.0,      0.0,    0.0, MAXREAL,  OPT_REAL,   0,   COIN_REAL_MIPINFWEIGHT}, 
	{"MipCutoffIncrement",     "MipCutIncr",   GRP_MIPTOL,      1e-5,     1e-5,    0.0,     1.0,  OPT_REAL,   0,   COIN_REAL_MIPCUTOFF}, 
	{"MipAllowableGap",        "MipAbsGap",    GRP_MIPTOL,     1e-10,    1e-10,    0.0, MAXREAL,  OPT_REAL,   0,   COIN_REAL_MIPABSGAP}, 
	{"MipFractionalGap",       "MipFracGap",   GRP_MIPTOL,     1e-10,    1e-10,    0.0,     1.0,  OPT_REAL,   0,   COIN_REAL_MIPFRACGAP}, 

	/* Probing */
	{"MipCutProbing",          "CutProbing",   GRP_MIPCUTS,        1,        1,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCUT_PROBING},
	{"MipProbeFrequency",      "ProbeFreq",    GRP_MIPCUTS,       -1,       -1,-MAXINT,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPPROBE_FREQ},
	{"MipProbeMode",           "ProbeMode",    GRP_MIPCUTS,        1,        1,      0,       2,  OPT_LIST,   0,   COIN_INT_MIPPROBE_MODE},
	{"MipProbeUseObjective",   "UseObject",    GRP_MIPCUTS,        1,        1,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPPROBE_USEOBJ},
	{"MipProbeMaxPass",        "MaxPass",      GRP_MIPCUTS,        3,        3,      0,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPPROBE_MAXPASS},
	{"MipProbeMaxProbe",       "MaxProbe",     GRP_MIPCUTS,      100,      100,      0,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPPROBE_MAXPROBE},
	{"MipProbeMaxLook",        "MaxLook",      GRP_MIPCUTS,       50,       50,      0,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPPROBE_MAXLOOK},
	{"MipProbeRowCuts",        "RowCuts",      GRP_MIPCUTS,        3,        3,      0,       3,  OPT_LIST,   0,   COIN_INT_MIPPROBE_ROWCUTS},

	/* Gomory cuts */
  	{"MipCutGomory",           "CutGomory",    GRP_MIPCUTS,        1,        1,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCUT_GOMORY},
	{"MipGomoryFrequency",     "GomoryFreq",   GRP_MIPCUTS,       -1,       -1,-MAXINT,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPGOMORY_FREQ},
	{"MipGomoryLimit",         "GomoryLimit",  GRP_MIPCUTS,       50,       50,      1,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPGOMORY_LIMIT},
	{"MipGomoryAway",          "GomoryAway",   GRP_MIPCUTS,     0.05,     0.05, 0.0001,     0.5,  OPT_REAL,   0,   COIN_REAL_MIPGOMORY_AWAY},

	/* Knapsack cuts */
	{"MipCutKnapsack",         "CutKnapsack",  GRP_MIPCUTS,        1,        1,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCUT_KNAPSACK},
	{"MipKnapsackFrequency",   "KrapsackFreq", GRP_MIPCUTS,       -1,       -1,-MAXINT,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPKNAPSACK_FREQ},
	{"MipKnapsackMaxIn",       "KnapsackMaxIn",GRP_MIPCUTS,       50,       50,      1,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPKNAPSACK_MAXIN},

	/* Oddhole cuts */
	{"MipCutOddHole",          "CutOddHole",   GRP_MIPCUTS,        0,        0,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCUT_ODDHOLE},
	{"MipOddHoleFrequency",    "OddHoleFreq",  GRP_MIPCUTS,       -1,       -1,-MAXINT,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPODDHOLE_FREQ},
	{"MipOddHoleMinViolation", "MinViolation", GRP_MIPCUTS,    0.001,    0.001,1.01e-8,     0.5,  OPT_REAL,   0,   COIN_REAL_MIPODDHOLE_MINVIOL},
	{"MipOddHoleMinViolPer",   "MinViolPer",   GRP_MIPCUTS,   0.0003,   0.0003,1.01e-8,    0.25,  OPT_REAL,   0,   COIN_REAL_MIPODDHOLE_MINVIOLPER},
	{"MipOddHoleMaxEntries",   "MaxEntries",   GRP_MIPCUTS,      100,      100,      3,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPODDHOLE_MAXENTRIES},

	/* Clique cuts */
	{"MipCutClique",           "CutClique",    GRP_MIPCUTS,        1,        1,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCUT_CLIQUE},
	{"MipCliqueFrequency",     "CliqueFreq",   GRP_MIPCUTS,       -1,       -1,-MAXINT,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPCLIQUE_FREQ},
	{"MipCliquePacking",       "CliquePacking",GRP_MIPCUTS,        0,        0,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCLIQUE_PACKING},
	{"MipCliqueStar",          "CliqueStar",   GRP_MIPCUTS,        1,        1,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCLIQUE_STAR},
	{"MipCliqueStarMethod",    "StarMethod",   GRP_MIPCUTS,        2,        2,      0,       2,  OPT_LIST,   0,   COIN_INT_MIPCLIQUE_STARMETHOD},
	{"MipCliqueStarMaxLen",    "StarMaxLen",   GRP_MIPCUTS,       12,       12,      0,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPCLIQUE_STARMAXLEN},
	{"MipCliqueStarReport",    "StarReport",   GRP_MIPCUTS,        1,        1,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCLIQUE_STARREPORT},
	{"MipCliqueRow",           "CliqueRow",    GRP_MIPCUTS,        1,        1,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCLIQUE_ROW},
	{"MipCliqueRowMaxLen",     "ClqRowMaxLen", GRP_MIPCUTS,       12,       12,      0,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPCLIQUE_ROWMAXLEN},
	{"MipCliqueRowReport",     "ClqRowReport", GRP_MIPCUTS,        1,        1,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCLIQUE_ROWREPORT},
	{"MipCliqueMinViolation",  "ClqMinViol",   GRP_MIPCUTS,     -1.0,     -1.0,-MAXREAL,MAXREAL,  OPT_REAL,   0,   COIN_REAL_MIPCLIQUE_MINVIOL},

	/* Lift and Project */
	{"MipCutLiftAndProject",   "CutLiftPro",   GRP_MIPCUTS,        0,        0,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCUT_LIFTPROJECT},
	{"MipLiftCoverFrequency",  "LiftProFreq",  GRP_MIPCUTS,       -1,       -1,-MAXINT,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPLIFTPRO_FREQ},
	{"MipLiftCoverBetaOne",    "LiftProBeta",  GRP_MIPCUTS,        1,        1,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPLIFTPRO_BETAONE},

	/* Simple Rounding */
	{"MipCutSimpleRounding",   "CutSimpRound", GRP_MIPCUTS,        0,        0,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPCUT_SIMPROUND},
	{"MipSimpleRoundFrequency","SimpRoundFreq",GRP_MIPCUTS,       -1,       -1,-MAXINT,  MAXINT,  OPT_INT,    0,   COIN_INT_MIPSIMPROUND_FREQ},

	{"MipUseCbcMain",          "UseCbcMain",   GRP_MIPSTRAT,       1,        1,      0,       1,  OPT_ONOFF,  0,   COIN_INT_MIPUSECBCMAIN}
	};

int CbcGetOptionCount(void)
{
	return OPTIONCOUNT;
}


SOLVOPTINFO *CbcGetOptionTable(void)
{
	return &OptionTable[0];
}



int CbcGetOptionID(int OptionNr)
{
	if ((OptionNr < 0) || (OptionNr >= OPTIONCOUNT)) {
		return -1;
	}
	return OptionTable[OptionNr].OptionID;
}

int CbcLocateOptionID(int OptionID)
{
	int i;
	SOLVOPTINFO *OptionTable;

	OptionTable = CbcGetOptionTable();
	for (i = 0; i < CbcGetOptionCount(); i++) {
		if (OptionID == OptionTable[i].OptionID) {
			return i;
		}
	}
	return -1;
}

SOLVOPTINFO *CbcGetOptionEntry(int OptionID)
{
	int OptionNr;
	SOLVOPTINFO *OptionTable;

	OptionTable = CbcGetOptionTable();
	OptionNr = CbcLocateOptionID(OptionID);
	if (OptionNr < 0) {
		return NULL;
	}
	return &OptionTable[OptionNr];
}


int CbcGetOptionChanged(int OptionID)
{
	SOLVOPTINFO *OptionEntry;

	OptionEntry = CbcGetOptionEntry(OptionID);
	if (!OptionEntry) {
		return 0;
	}
	return OptionEntry->changed;
}


int CbcGetIntOption(int OptionID)
{   
	SOLVOPTINFO *OptionEntry;

	OptionEntry = CbcGetOptionEntry(OptionID);
	if (!OptionEntry) {
		return 0;
	}
	if (OptionEntry->OptionType == OPT_REAL) {
		return 0;
	}
	return (int)OptionEntry->CurrentValue;
}
  	
int CbcSetIntOption(int OptionID, int IntValue)
{
	SOLVOPTINFO *OptionEntry;

	OptionEntry = CbcGetOptionEntry(OptionID);
	if (!OptionEntry) {
		return CBC_CALL_FAILED;
	}
	if (OptionEntry->OptionType == OPT_REAL) {
		return CBC_CALL_FAILED;
	}
	//coinWriteMsgLog("%s[%d] = %d (was %d)",OptionEntry->OptionName, OptionNr, IntValue, (int)OptionEntry->CurrentValue);
	OptionEntry->CurrentValue = IntValue;
	OptionEntry->changed = 1;
	return CBC_CALL_SUCCESS;
}


double CbcGetRealOption(int OptionID)
{
	SOLVOPTINFO *OptionEntry;

	OptionEntry = CbcGetOptionEntry(OptionID);
	if (!OptionEntry) {
		return 0.0;
	}
	if (OptionEntry->OptionType != OPT_REAL) {
		return 0.0;
	}
	return OptionEntry->CurrentValue;
}


int CbcSetRealOption(int OptionID, double RealValue)
{
	SOLVOPTINFO *OptionEntry;

	OptionEntry = CbcGetOptionEntry(OptionID);
	if (!OptionEntry) {
		return CBC_CALL_FAILED;
	}
	if (OptionEntry->OptionType != OPT_REAL) {
		return CBC_CALL_FAILED;
	}
	//coinWriteMsgLog("%s[%d] = %lg (was %lg)",OptionTable[OptionNr].OptionName, OptionNr, RealValue, OptionTable[OptionNr].CurrentValue);
	OptionEntry->CurrentValue = RealValue;
	OptionEntry->changed = 1;
	return CBC_CALL_SUCCESS;
}

//double
// DualTolerance
// PrimalTolerance
// DualBound
// PrimalWeight (setInfeasibilityCost)
// MaxTime      (setMaximumSeconds)
// ObjScale     (setObjectiveScale)
// RhsScale     (setRhsScale)

//int
//LogLevel      (setLogLovel/model->factorization()->messageLevel(8);)
//MaxFactor     (model->factorization()->maximumPivots(value);
//PertValue     (setPerturbation)
//MaxIteration  (setMaximumIterations)

//AutoScale     off
//Barrier
//BiasLU        UU  (UX, LX, LL)
//Crash         off
//Direction     min
//Directory
//dualBound     1e-20, 1e12
//dualPivot     Automatic, (Dantzig, partial, Steepest)
//DualSimplex
//dualTolerance 1e-20,1e12
//Exit
//ErrorsAllowed
//FakeBound
//Help
//Idiot
//Import
//KeepNames
//LogLevel
//Maximize
//MaxFactor     1, 999999
//MaxIteration  0, 99999999
//Messages
//ObjScale      -1e20, 1e20
//PresolvePass  0, 100
//PertValue     -5000, 102, false
//Perturbation  On, off
//PlusMinus     -1, false
//Presolve      On
//PrimalPivot     Automatic, (Dantzig, partial, Steepest, change, sprint)
//PrimalSimplex
//PrimalTolerance 1e-20,1e12
//PrimalWeight    1e20, 1e20
//RhsScale        -1e20, 1.20
//Scaling         Off, Equilibrium, geometric, automatic
//MaxTime         0.0, 1e12
//SparseFactor    On, Off
//SprintCrash     -1, 500

