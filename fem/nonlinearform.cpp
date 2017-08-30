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

#include "fem.hpp"

namespace mfem
{

void NonlinearForm::SetEssentialBC(const Array<int> &bdr_attr_is_ess,
                                   Vector *rhs)
{
   int i, j, vsize, nv;
   vsize = fes->GetVSize();
   Array<int> vdof_marker(vsize);

   // virtual call, works in parallel too
   fes->GetEssentialVDofs(bdr_attr_is_ess, vdof_marker);
   nv = 0;
   for (i = 0; i < vsize; i++)
      if (vdof_marker[i])
      {
         nv++;
      }

   ess_vdofs.SetSize(nv);

   for (i = j = 0; i < vsize; i++)
      if (vdof_marker[i])
      {
         ess_vdofs[j++] = i;
      }

   if (rhs)
      for (i = 0; i < nv; i++)
      {
         (*rhs)(ess_vdofs[i]) = 0.0;
      }
}

double NonlinearForm::GetEnergy(const Vector &x) const
{
   Array<int> vdofs;
   Vector el_x;
   const FiniteElement *fe;
   ElementTransformation *T;
   double energy = 0.0;

   if (dfi.Size())
      for (int i = 0; i < fes->GetNE(); i++)
      {
         fe = fes->GetFE(i);
         fes->GetElementVDofs(i, vdofs);
         T = fes->GetElementTransformation(i);
         x.GetSubVector(vdofs, el_x);
         for (int k = 0; k < dfi.Size(); k++)
         {
            energy += dfi[k]->GetElementEnergy(*fe, *T, el_x);
         }
      }

   return energy;
}

void NonlinearForm::Mult(const Vector &x, Vector &y) const
{
   Array<int> vdofs;
   Vector el_x, el_y;
   const FiniteElement *fe;
   ElementTransformation *T;

   y = 0.0;

   if (dfi.Size())
      for (int i = 0; i < fes->GetNE(); i++)
      {
         fe = fes->GetFE(i);
         fes->GetElementVDofs(i, vdofs);
         T = fes->GetElementTransformation(i);
         x.GetSubVector(vdofs, el_x);
         for (int k = 0; k < dfi.Size(); k++)
         {
            dfi[k]->AssembleElementVector(*fe, *T, el_x, el_y);
            y.AddElementVector(vdofs, el_y);
         }
      }

   for (int i = 0; i < ess_vdofs.Size(); i++)
   {
      y(ess_vdofs[i]) = 0.0;
   }
   // y(ess_vdofs[i]) = x(ess_vdofs[i]);
}

Operator &NonlinearForm::GetGradient(const Vector &x) const
{
   const int skip_zeros = 0;
   Array<int> vdofs;
   Vector el_x;
   DenseMatrix elmat;
   const FiniteElement *fe;
   ElementTransformation *T;

   if (Grad == NULL)
   {
      Grad = new SparseMatrix(fes->GetVSize());
   }
   else
   {
      *Grad = 0.0;
   }

   if (dfi.Size())
      for (int i = 0; i < fes->GetNE(); i++)
      {
         fe = fes->GetFE(i);
         fes->GetElementVDofs(i, vdofs);
         T = fes->GetElementTransformation(i);
         x.GetSubVector(vdofs, el_x);
         for (int k = 0; k < dfi.Size(); k++)
         {
            dfi[k]->AssembleElementGrad(*fe, *T, el_x, elmat);
            Grad->AddSubMatrix(vdofs, vdofs, elmat, skip_zeros);
            // Grad->AddSubMatrix(vdofs, vdofs, elmat, 1);
         }
      }

   for (int i = 0; i < ess_vdofs.Size(); i++)
   {
      Grad->EliminateRowCol(ess_vdofs[i]);
   }

   if (!Grad->Finalized())
   {
      Grad->Finalize(skip_zeros);
   }

   return *Grad;
}

NonlinearForm::~NonlinearForm()
{
   delete Grad;
   for (int i = 0; i < dfi.Size(); i++)
   {
      delete dfi[i];
   }
}

MixedNonlinearForm::MixedNonlinearForm(Array<FiniteElementSpace *>f)
{
   height = 0;
   width = 0;
   fes.Copy(f);
   block_offsets.SetSize(f.Size() + 1);
   block_trueOffsets.SetSize(f.Size() + 1);
   block_offsets[0] = 0;
   block_trueOffsets[0] = 0;

   for (int i=0; i<fes.Size(); i++) {
      block_offsets[i+1] = fes[i]->GetVSize();
      block_trueOffsets[i+1] = fes[i]->GetTrueVSize();
   }

   block_offsets.PartialSum();
   block_trueOffsets.PartialSum();

   height = block_trueOffsets[fes.Size() + 1];
   width = block_trueOffsets[fes.Size() + 1];

   Grads.SetSize(fes.Size(), fes.Size());
   for (int i=0; i<fes.Size(); i++) {
      for (int j=0; j<fes.Size(); j++) {
         Grads(i,j) = NULL;
      }
   }
}

void MixedNonlinearForm::AddBdrFaceIntegrator(MixedNonlinearFormIntegrator *fi,
                                              Array<int> &bdr_attr_marker)
{
   ffi.Append(fi);
   ffi_marker.Append(&bdr_attr_marker);
}

   
void MixedNonlinearForm::SetEssentialBC(const Array<Array<int> >&bdr_attr_is_ess,
                                        Array<Vector *>rhs)
{
   int i, j, vsize, nv;

   for (int s=0; s<fes.Size(); s++) {
      // First, set u variables
      vsize = fes[s]->GetVSize();
      Array<int> vdof_marker(vsize);

      // virtual call, works in parallel too
      fes[s]->GetEssentialVDofs(bdr_attr_is_ess[s], vdof_marker);
      nv = 0;
      for (i = 0; i < vsize; i++) {
         if (vdof_marker[i]) {
            nv++;
         }
      }
      
      ess_vdofs[s].SetSize(nv);

      for (i = j = 0; i < vsize; i++) {
         if (vdof_marker[i]) {
            ess_vdofs[s][j++] = i;
         }
      }

      if (rhs[s]) {
         for (i = 0; i < nv; i++) {
            (*rhs[s])(ess_vdofs[i]) = 0.0;
         }
      }
   }
}
   
void MixedNonlinearForm::Mult(const BlockVector &x, BlockVector &y) const
{
   Array<Array<int> >vdofs(fes.Size());
   Array<Vector> el_x(fes.Size()), el_y(fes.Size());
   const Array<FiniteElement *> fe(fes.Size());
   Array<ElementTransformation *> T(fes.Size());

   Array<Vector> xs(fes.Size()), ys(fes.Size());

   for (int i=0; i<fes.Size(); i++) {
      xs[i] = x.GetBlock(i);
      ys[i] = y.GetBlock(i);
      ys[i] = 0.0;
   }

   if (dfi.Size()) {
      for (int i = 0; i < fes[0]->GetNE(); i++) {
         for (int s = 0; s < fes.Size(); s++) {
            
            fe[s] = fes[s]->GetFE(i);
            fes[s]->GetElementVDofs(i, vdofs[s]);
            T[s] = fes[s]->GetElementTransformation(i);
            xs[s].GetSubVector(vdofs[s], el_x[s]);
         }
         
         for (int k = 0; k < dfi.Size(); k++)
         {
            dfi[k]->AssembleElementVector(*u_fe, *T, 
                                          el_x, el_y);

            for (int s=0; s<fes.Size(); s++) {
               ys[s].AddElementVector(vdofs[s], el_y[s]);
            }
         }
      }
   }
   
   if (bfi.Size()) {
      for (int i = 0; i < fes[0]->GetNBE(); i++) {
         for (int s=0; s<fes.Size(); s++) {
            fe[s] = fes[s]->GetBE(i);
            fes[s]->GetBdrElementVDofs(i, vdofs[s]);
            T[s] = fes[s]->GetBdrElementTransformation(i);
            xs[s].GetSubVector(vdofs[s], el_x[s]);
         }
         
         for (int k = 0; k < bfi.Size(); k++) {
            bfi[k]->AssembleElementVector(*fe, *T, 
                                          el_x, el_y);

            for (int s = 0; s < fes.Size(); s++) { 
               ys[s].AddElementVector(vdofs[s], el_y[s]);
            }
         }
      }
   }
   
   if (ffi.Size()) {
      Mesh *mesh = u_fes->GetMesh();
      FaceElementTransformations *tr;
      // Which boundary attributes need to be processed?
      Array<int> bdr_attr_marker(mesh->bdr_attributes.Size() ?
                                 mesh->bdr_attributes.Max() : 0);
      bdr_attr_marker = 0;
      for (int k = 0; k < ffi.Size(); k++)
      {
         if (ffi_marker[k] == NULL)
         {
            bdr_attr_marker = 1;
            break;
         }
         Array<int> &bdr_marker = *ffi_marker[k];
         MFEM_ASSERT(bdr_marker.Size() == bdr_attr_marker.Size(),
                     "invalid boundary marker for boundary face integrator #"
                     << k << ", counting from zero");
         for (int i = 0; i < bdr_attr_marker.Size(); i++)
            {
               bdr_attr_marker[i] |= bdr_marker[i];
            }
      }      
      
      for (int i = 0; i < mesh->GetNBE(); i++) {
         const int bdr_attr = mesh->GetBdrAttribute(i);
         if (bdr_attr_marker[bdr_attr-1] == 0) { continue; }

         tr = mesh->GetBdrFaceTransformations(i);
         if (tr != NULL) {
            for (int s=0; s<fes.Size(); s++) {
               fe[s] = fes[s]->GetFE(tr->Elem1No);
               fes[s]->GetElementVDofs(tr->Elem1No, vdofs[s]);
               xs[s].GetSubVector(vdofs[s], el_x[s]);
            }

            for (int k = 0; k < ffi.Size(); k++) {
               if (ffi_marker[k] &&
                   (*ffi_marker[k])[bdr_attr-1] == 0) { continue; }

               ffi[k]->AssembleRHSElementVector(fe, *tr, 
                                                el_x, el_y);

               for (int s=0; s<fes.Size(); s++) {
                  ys[s].AddElementVector(vdofs[s], el_y[s]);
               }
            }
         }
      }
   }
      

   for (int s=0; s<fes.Size(); s++) {
      for (int i = 0; i < ess_vdofs[s].Size(); i++) {
         ys[s](ess_vdofs[s][i]) = 0.0;
      }
   }
}
 
void MixedNonlinearForm::Mult(const Vector &x, Vector &y) const
{
   mfem_error("MixedNonlinearFormIntegrator::Mult(Vector, Vector)"
              " is not overloaded!");
}

Operator &MixedNonlinearForm::GetGradient(const BlockVector &x, BlockVector &y) const
{
   const int skip_zeros = 0;
   Array<Array<int> >vdofs(fes.Size());
   Array<Vector> el_x(fes.Size());
   Array2D<DenseMatrix> elmats(fes.Size(), fes.Size());
   const Array<FiniteElement *>fe(fes.Size());
   Array<ElementTransformation *>T(fes.Size());

   if (BlockGrad == NULL)
   {
      BlockGrad = new BlockOperator(block_offsets);
   }

   if (Grads(0,0) != NULL) {
      for (int i=0; i<fes.Size(); i++) {
         for (int j=0; j<fes.Size(); j++) {
            delete Grads(i,j);
            Grads(i,j) = new SparseMatrix(fes[i]->GetVSize(), fes[j]->GetVSize());
         }
      }
   }

   if (dfi.Size()) {
      for (int i = 0; i < u_fes->GetNE(); i++) {
         for (int s = 0; s < fes.Size(); s++) {
            fe[s] = fes[s]->GetFE(i);
            fes[s]->GetElementVDofs(i, vdofs[s]);
            T[s] = fes[s]->GetElementTransformation(i);
            xs[s].GetSubVector(vdofs[s], el_x[s]);
         }
         
         for (int k = 0; k < dfi.Size(); k++) {
            dfi[k]->AssembleElementGrad(*fe, *T, 
                                        el_x, elmats);
            for (int i=0; i<fes.Size(); i++) {
               Grads(i,j)->AddSubMatrix(vdofs[i], vdofs[j], elmats(i,j), skip_zeros);
            }
         }
      }
   }
   if (bfi.Size()) {
      for (int i = 0; i < fes[0]->GetNBE(); i++) {
         for (int s=0; s < fes.Size(); s++) {
            fe[s] = fes[s]->GetBE(i);
            fes[s]->GetBdrElementVDofs(i, vdofs[s]);
            xs[s].GetSubVector(vdofs[s], el_x[s]);
            T[s] = fes[s]->GetBdrElementTransformation(i);
         }

         for (int k = 0; k < dfi.Size(); k++) {
            bfi[k]->AssembleElementGrad(*fe, *T, 
                                        el_x, elmats);
            for (int i=0; i<fes.Size(); i++) {
               for (int j=0; j<fes.Size(); j++) {
                  Grads(i,j)->AddSubMatrix(vdofs[i], vdofs[j], elmats(i,j), skip_zeros);
               }
            }
         }
      }
   }         
         
   if (ffi.Size()) {
      FaceElementTransformations *tr;
      Mesh *mesh = fes[0]->GetMesh();

      // Which boundary attributes need to be processed?
      Array<int> bdr_attr_marker(mesh->bdr_attributes.Size() ?
                                 mesh->bdr_attributes.Max() : 0);
      bdr_attr_marker = 0;
      for (int k = 0; k < ffi.Size(); k++)
      {
         if (ffi_marker[k] == NULL)
         {
            bdr_attr_marker = 1;
            break;
         }
         Array<int> &bdr_marker = *ffi_marker[k];
         MFEM_ASSERT(bdr_marker.Size() == bdr_attr_marker.Size(),
                     "invalid boundary marker for boundary face integrator #"
                     << k << ", counting from zero");
         for (int i = 0; i < bdr_attr_marker.Size(); i++)
            {
               bdr_attr_marker[i] |= bdr_marker[i];
            }
      }      
   
      for (int i = 0; i < mesh->GetNBE(); i++)
      {
         const int bdr_attr = mesh->GetBdrAttribute(i);
         if (bdr_attr_marker[bdr_attr-1] == 0) { continue; }

         tr = mesh->GetBdrFaceTransformations(i);
         if (tr != NULL) {
            for (int s = 0; s < fes.Size(); s++) {
               fe[s] = fes[s]->GetFE(i);
               fes[s]->GetElementVDofs(i, vdofs[s]);
               T[s] = fes[s]->GetElementTransformation(i);
               xs[s].GetSubVector(vdofs[s], el_x[s]);
            }

            for (int k = 0; k < dfi.Size(); k++) {
               ffi[k]->AssembleElementGrad(*fe, *T, 
                                           el_x, elmats);
               for (int i=0; i<fes.Size(); i++) {
                  for (int j=0; j<fes.Size(); j++) {
                     Grads(i,j)->AddSubMatrix(vdofs[i], vdofs[j], elmats(i,j), skip_zeros);
                  }
               }
            }
         }
      }
   }

   for (int s=0; s<fes.Size(); s++) {
      for (int i = 0; i < vdofs[s].Size(); i++)
      {
         for (int j=0; j<fes.Size(); J++) {
            if (s==j) {
               Grads(s,s)->EliminateRowCol(ess_vdofs[s][i], 1);
            }
            else {
               Grads(s,j)->EliminateRow(ess_vdofs[s][i]);
               Grads(j,s)->EliminateCol(ess_vdofs[s][i]);
            }
         }
      }
      
      if (Grads(0,0)->Finalized()) {
         for (int i=0; i<fes.Size(); i++) {
            for (int j=0; j<fes.Size(); j++) {
               Grads(i,j)->Finalize(skip_zeros);
            }
         }
      }
   }

   for (int i=0; i<fes.Size(); i++) {
      for (int j=0; j<fes.Size(); j++) {
         BlockGrad->SetBlock(i,j,Grads(i,j));
      }
   }

   return *BlockGrad;
}

MixedNonlinearForm::~MixedNonlinearForm()
{
   delete BlockGrad;
   
   for (int i=0; i<fes.Size(); i++) {
      delete Grads(i,j);
   }

   for (int i = 0; i < dfi.Size(); i++)
      {
         delete dfi[i];
      }

   for (int i = 0; i < bfi.Size(); i++)
      {
         delete bfi[i];
      }

   for (int i = 0; i < bfi.Size(); i++)
      {
         delete ffi[i];
      }

}

}
