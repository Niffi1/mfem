// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#include "../config/config.hpp"

#ifdef MFEM_USE_MPI

#include "fem.hpp"
#include "bilininteg.hpp"
#include "pbilinearform.hpp"
#include "kBilinIntegDiffusion.hpp"
#include "kfespace.hpp"
#include "../linalg/kernels/vector.hpp"

#include <cmath>

// *****************************************************************************
namespace mfem
{

// ***************************************************************************
// * ParPABilinearForm
// ***************************************************************************
ParPABilinearForm::ParPABilinearForm(ParFiniteElementSpace *fes) :
   PABilinearForm(fes),
   mesh(fes->GetMesh()),
   trialFes(fes),
   testFes(fes),
   localX(mesh->GetNE() * trialFes->GetFE(0)->GetDof() * trialFes->GetVDim()),
   localY(mesh->GetNE() * testFes->GetFE(0)->GetDof() * testFes->GetVDim()),
   kfes(new kFiniteElementSpace(static_cast<FiniteElementSpace*>(fes))) { }

// ***************************************************************************
ParPABilinearForm::~ParPABilinearForm() { /*delete kfes;*/}

// *****************************************************************************
void ParPABilinearForm::EnableStaticCondensation() { assert(false);}

// ***************************************************************************
// Adds new Domain Integrator.
void ParPABilinearForm::AddDomainIntegrator(AbstractBilinearFormIntegrator *i)
{
   integrators.Append(static_cast<BilinearPAFormIntegrator*>(i));
}

// Adds new Boundary Integrator.
void ParPABilinearForm::AddBoundaryIntegrator(AbstractBilinearFormIntegrator *i)
{
   assert(false);
   //AddIntegrator(i, BoundaryIntegrator);
}

// Adds new interior Face Integrator.
void ParPABilinearForm::AddInteriorFaceIntegrator(AbstractBilinearFormIntegrator
                                                  *i)
{
   assert(false);
   //AddIntegrator(i, InteriorFaceIntegrator);
}

// Adds new boundary Face Integrator.
void ParPABilinearForm::AddBoundaryFaceIntegrator(AbstractBilinearFormIntegrator
                                                  *i)
{
   assert(false);
   //AddIntegrator(i, BoundaryFaceIntegrator);
}

// *****************************************************************************
// * WARNING DiffusionGetRule
// *****************************************************************************
static const IntegrationRule &DiffusionGetRule(const FiniteElement &trial_fe,
                                               const FiniteElement &test_fe)
{
   int order;
   if (trial_fe.Space() == FunctionSpace::Pk)
   {
      order = trial_fe.GetOrder() + test_fe.GetOrder() - 2;
   }
   else
   {
      // order = 2*el.GetOrder() - 2;  // <-- this seems to work fine too
      order = trial_fe.GetOrder() + test_fe.GetOrder() + trial_fe.GetDim() - 1;
   }
   if (trial_fe.Space() == FunctionSpace::rQk)
   {
      return RefinedIntRules.Get(trial_fe.GetGeomType(), order);
   }
   return IntRules.Get(trial_fe.GetGeomType(), order);
}

// ***************************************************************************
void ParPABilinearForm::Assemble(int skip_zeros)
{
   //const int nbi = integrators.Size();
   assert(integrators.Size()==1);
   const FiniteElement &fe = *fes->GetFE(0);
   const IntegrationRule *ir = &DiffusionGetRule(fe,fe);
   //const int order = ir->GetOrder();
   assert(ir);
   const int integratorCount = integrators.Size();
   for (int i = 0; i < integratorCount; ++i)
   {
      integrators[i]->Setup(fes,ir);
      integrators[i]->Assemble();
   }
}

// ***************************************************************************
void ParPABilinearForm::FormOperator(const Array<int> &ess_tdof_list,
                                     Operator &A)
{
   assert(false);
   const Operator* trialP = trialFes->GetProlongationMatrix();
   const Operator* testP  = testFes->GetProlongationMatrix();
   Operator *rap = this;
   if (trialP) { rap = new RAPOperator(*testP, *this, *trialP); }
   const bool own_A = rap!=this;
   assert(rap);
   Operator *CO = new ConstrainedOperator(rap, ess_tdof_list, own_A);
   A = *CO;
}

// ***************************************************************************
void ParPABilinearForm::FormLinearSystem(const Array<int> &ess_tdof_list,
                                         Vector &x, Vector &b,
                                         Operator *&A, Vector &X, Vector &B,
                                         int copy_interior)
{
   const Operator* trialP = trialFes->GetProlongationMatrix();
   const Operator* testP  = testFes->GetProlongationMatrix();
   Operator *rap = this;
   if (trialP) { rap = new RAPOperator(*testP, *this, *trialP); }
   const bool own_A = rap!=this;
   assert(rap);

   A = new ConstrainedOperator(rap, ess_tdof_list, own_A);

   const Operator* P = trialFes->GetProlongationMatrix();
   const Operator* R = trialFes->GetRestrictionMatrix();
   if (P)
   {
      // Variational restriction with P
      B.SetSize(P->Width());
      P->MultTranspose(b, B);
      X.SetSize(R->Height());
      R->Mult(x, X);
   }
   else
   {
      // rap, X and B point to the same data as this, x and b
      X.SetSize(x.Size()); X = x;
      B.SetSize(b.Size()); B = b;
   }

   if (!copy_interior and ess_tdof_list.Size()>0)
   {
      const int csz = ess_tdof_list.Size();
      Vector subvec(csz);
      subvec = 0.0;
      kVectorGetSubvector(csz,
                          subvec.GetData(),
                          X.GetData(),
                          ess_tdof_list.GetData());
      X = 0.0;
      kVectorSetSubvector(csz,
                          X.GetData(),
                          subvec.GetData(),
                          ess_tdof_list.GetData());
   }

   ConstrainedOperator *cA = static_cast<ConstrainedOperator*>(A);
   assert(cA);
   if (cA)
   {
      cA->EliminateRHS(X, B);
   }
   else
   {
      mfem_error("BilinearForm::InitRHS expects an ConstrainedOperator");
   }
}

// ***************************************************************************
void ParPABilinearForm::Mult(const Vector &x, Vector &y) const
{
   kfes->GlobalToLocal(x, localX);
   localY = 0.0;
   const int iSz = integrators.Size();
   assert(iSz==1);
   for (int i = 0; i < iSz; ++i)
   {
      integrators[i]->MultAdd(localX, localY);
   }
   kfes->LocalToGlobal(localY, y);
}

// ***************************************************************************
void ParPABilinearForm::MultTranspose(const Vector &x, Vector &y) const
{
   assert(false);
   kfes->GlobalToLocal(x, localX);
   localY = 0.0;
   const int iSz = integrators.Size();
   assert(iSz==1);
   for (int i = 0; i < iSz; ++i)
   {
      integrators[i]->MultTransposeAdd(localX, localY);
   }
   kfes->LocalToGlobal(localY, y);
}

// ***************************************************************************
void ParPABilinearForm::RecoverFEMSolution(const Vector &X,
                                           const Vector &b,
                                           Vector &x)
{
   const Operator *P = trialFes->GetProlongationMatrix();
   if (P)
   {
      // Apply conforming prolongation
      x.SetSize(P->Height());
      P->Mult(X, x);
      return;
   }
   // Otherwise X and x point to the same data
   x = X;
}

// *****************************************************************************
} // mfem

#endif // MFEM_USE_MPI