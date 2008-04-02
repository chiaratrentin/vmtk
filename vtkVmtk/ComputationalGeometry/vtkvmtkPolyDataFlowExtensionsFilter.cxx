/*=========================================================================

Program:   VMTK
Module:    $RCSfile: vtkvmtkPolyDataFlowExtensionsFilter.cxx,v $
Language:  C++
Date:      $Date: 2006/07/07 10:46:19 $
Version:   $Revision: 1.12 $

  Copyright (c) Luca Antiga, David Steinman. All rights reserved.
  See LICENCE file for details.

  Portions of this code are covered under the VTK copyright.
  See VTKCopyright.txt or http://www.kitware.com/VTKCopyright.htm 
  for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/

#include "vtkvmtkPolyDataFlowExtensionsFilter.h"
#include "vtkvmtkPolyDataBoundaryExtractor.h"
#include "vtkvmtkBoundaryReferenceSystems.h"
#include "vtkvmtkPolyBallLine.h"
#include "vtkvmtkMath.h"
#include "vtkThinPlateSplineTransform.h"
#include "vtkTransform.h"
#include "vtkPolyLine.h"
#include "vtkPointData.h"
#include "vtkDoubleArray.h"
#include "vtkIntArray.h"
#include "vtkMath.h"
#include "vtkCellArray.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"

vtkCxxRevisionMacro(vtkvmtkPolyDataFlowExtensionsFilter, "$Revision: 1.12 $");
vtkStandardNewMacro(vtkvmtkPolyDataFlowExtensionsFilter);

vtkvmtkPolyDataFlowExtensionsFilter::vtkvmtkPolyDataFlowExtensionsFilter()
{
  this->Centerlines = NULL;
  this->ExtensionRatio = 1.0;
  this->TransitionRatio = 0.5;
  this->ExtensionLength = 0.0;
  this->ExtensionRadius = 1.0;
  this->CenterlineNormalEstimationDistanceRatio = 1.0;
  this->AdaptiveExtensionLength = 1;
  this->AdaptiveExtensionRadius = 1;
  this->NumberOfBoundaryPoints = 50;
  this->BoundaryIds = NULL;
  this->Sigma = 1.0;
  this->SetExtensionModeToUseCenterlineDirection();
  this->SetInterpolationModeToThinPlateSpline();
}

vtkvmtkPolyDataFlowExtensionsFilter::~vtkvmtkPolyDataFlowExtensionsFilter()
{
  if (this->Centerlines)
    {
    this->Centerlines->Delete();
    this->Centerlines = NULL;
    }

  if (this->BoundaryIds)
    {
    this->BoundaryIds->Delete();
    this->BoundaryIds = NULL;
    }
}

int vtkvmtkPolyDataFlowExtensionsFilter::RequestData(
  vtkInformation *vtkNotUsed(request),
  vtkInformationVector **inputVector,
  vtkInformationVector *outputVector)
{
  vtkInformation *inInfo = inputVector[0]->GetInformationObject(0);
  vtkInformation *outInfo = outputVector->GetInformationObject(0);

  vtkPolyData *input = vtkPolyData::SafeDownCast(
    inInfo->Get(vtkDataObject::DATA_OBJECT()));
  vtkPolyData *output = vtkPolyData::SafeDownCast(
    outInfo->Get(vtkDataObject::DATA_OBJECT()));

  if (this->ExtensionMode == USE_CENTERLINE_DIRECTION)
    {
    if (!this->Centerlines)
      {
      vtkErrorMacro(<< "Centerlines not set.");
      return 1;
      }
    }

  vtkPoints* outputPoints = vtkPoints::New();
  vtkCellArray* outputPolys = vtkCellArray::New();

  outputPoints->DeepCopy(input->GetPoints());
  outputPolys->DeepCopy(input->GetPolys());

  vtkvmtkPolyDataBoundaryExtractor* boundaryExtractor = vtkvmtkPolyDataBoundaryExtractor::New();
  boundaryExtractor->SetInput(input);
  boundaryExtractor->Update();

  vtkPolyData* boundaries = boundaryExtractor->GetOutput();

  vtkPolyData* centerlines = vtkPolyData::New();
  vtkvmtkPolyBallLine* tube = vtkvmtkPolyBallLine::New();
  vtkDoubleArray* zeroRadiusArray = vtkDoubleArray::New();

  if (this->ExtensionMode == USE_CENTERLINE_DIRECTION)
    {
    centerlines->DeepCopy(this->Centerlines);

    const char zeroRadiusArrayName[] = "ZeroRadiusArray";

    zeroRadiusArray->SetName(zeroRadiusArrayName);
    zeroRadiusArray->SetNumberOfTuples(centerlines->GetNumberOfPoints());
    zeroRadiusArray->FillComponent(0,0.0);
    
    centerlines->GetPointData()->AddArray(zeroRadiusArray);
  
    tube->SetInput(centerlines);
    tube->SetPolyBallRadiusArrayName(zeroRadiusArrayName);
    }

  input->BuildCells();
  input->BuildLinks();

  int i, k;
  for (i=0; i<boundaries->GetNumberOfCells(); i++)
    {
    if (this->BoundaryIds)
      {
      if (this->BoundaryIds->IsId(i) == -1)
        {
        continue;
        }
      }
    
    vtkPolyLine* boundary = vtkPolyLine::SafeDownCast(boundaries->GetCell(i));

    if (!boundary)
      {
      vtkErrorMacro(<<"Boundary not a vtkPolyLine");
      continue;
      }

    int numberOfBoundaryPoints = boundary->GetNumberOfPoints();

    vtkIdList* boundaryIds = vtkIdList::New();
    int j;
    for (j=0; j<numberOfBoundaryPoints; j++)
      {
      boundaryIds->InsertNextId(static_cast<vtkIdType>(vtkMath::Round(boundaries->GetPointData()->GetScalars()->GetComponent(boundary->GetPointId(j),0))));
      }
    
    double barycenter[3];
    double normal[3], outwardNormal[3];
    double meanRadius;

    vtkvmtkBoundaryReferenceSystems::ComputeBoundaryBarycenter(boundary->GetPoints(),barycenter);
    meanRadius = vtkvmtkBoundaryReferenceSystems::ComputeBoundaryMeanRadius(boundary->GetPoints(),barycenter);
    vtkvmtkBoundaryReferenceSystems::ComputeBoundaryNormal(boundary->GetPoints(),barycenter,normal);
    vtkvmtkBoundaryReferenceSystems::OrientBoundaryNormalOutwards(input,boundaries,i,normal,outwardNormal);

    int boundaryDirection = 1;
    if (vtkMath::Dot(normal,outwardNormal) > 0.0)
      {
      boundaryDirection = -1;
      }

    double flowExtensionNormal[3];
    flowExtensionNormal[0] = flowExtensionNormal[1] = flowExtensionNormal[2] = 0.0;  
 
    if (this->ExtensionMode == USE_CENTERLINE_DIRECTION)
      {
      tube->EvaluateFunction(barycenter);
  
      double centerlinePoint[3];
      vtkIdType cellId, subId;
      double pcoord;
      tube->GetLastPolyBallCenter(centerlinePoint);
      cellId = tube->GetLastPolyBallCellId();
      subId = tube->GetLastPolyBallCellSubId();
      pcoord = tube->GetLastPolyBallCellPCoord();
  
      vtkCell* centerline = centerlines->GetCell(cellId);
  
      vtkIdType pointId0, pointId1;
      double abscissa;
  
      double point0[3], point1[3];
  
      pointId0 = 0;
      abscissa = sqrt(vtkMath::Distance2BetweenPoints(centerlinePoint,centerline->GetPoints()->GetPoint(subId)));
      for (j=subId-1; j>=0; j--)
        {
        centerline->GetPoints()->GetPoint(j,point0);
        centerline->GetPoints()->GetPoint(j+1,point1);
        abscissa += sqrt(vtkMath::Distance2BetweenPoints(point0,point1));
        if (abscissa > meanRadius * this->CenterlineNormalEstimationDistanceRatio)
          {
          pointId0 = j;
          break;
          }
        }
  
      pointId1 = centerline->GetNumberOfPoints()-1;
      abscissa = sqrt(vtkMath::Distance2BetweenPoints(centerlinePoint,centerline->GetPoints()->GetPoint(subId+1)));
      for (j=subId+1; j<centerline->GetNumberOfPoints()-2; j++)
        {
        centerline->GetPoints()->GetPoint(j,point0);
        centerline->GetPoints()->GetPoint(j+1,point1);
        abscissa += sqrt(vtkMath::Distance2BetweenPoints(point0,point1));
        if (abscissa > meanRadius * this->CenterlineNormalEstimationDistanceRatio)
          {
          pointId1 = j+1;
          break;
          }
        }
  
      // use an approximating spline or smooth centerline points to better catch the trend in computing centerlineNormal?
  
      double centerlineNormal[3];
  
      centerline->GetPoints()->GetPoint(pointId0,point0);
      centerline->GetPoints()->GetPoint(pointId1,point1);
  
      double toleranceFactor = 1E-4;
  
      for (k=0; k<3; k++)
        {
        centerlineNormal[k] = 0.0;
        }
      if (sqrt(vtkMath::Distance2BetweenPoints(point1,centerlinePoint)) > toleranceFactor*meanRadius)
        {
        for (k=0; k<3; k++)
          {
          centerlineNormal[k] += point1[k] - centerlinePoint[k];
          }
        } 
      if (sqrt(vtkMath::Distance2BetweenPoints(centerlinePoint,point0)) > toleranceFactor*meanRadius)
        {
        for (k=0; k<3; k++)
          {
          centerlineNormal[k] += centerlinePoint[k] - point0[k];
          }
        }
  
      vtkMath::Normalize(centerlineNormal);
  
      for (k=0; k<3; k++)
        {
        flowExtensionNormal[k] = centerlineNormal[k];
        }
  
      if (vtkMath::Dot(outwardNormal,centerlineNormal) < 0.0)
        {
        for (k=0; k<3; k++)
          {
          flowExtensionNormal[k] *= -1.0;
          }
        }
      }
    else if (this->ExtensionMode == USE_NORMAL_TO_BOUNDARY)
      {
      for (k=0; k<3; k++)
        {
        flowExtensionNormal[k] = outwardNormal[k];
        }
      }
    else
      {
      vtkErrorMacro(<< "Invalid ExtensionMode.");
      return 1;
      }

    double extensionLength;

    if (this->AdaptiveExtensionLength)
      {
      extensionLength = meanRadius * this->ExtensionRatio;
      }
    else
      {
      extensionLength = this->ExtensionLength;
      }

    double point[3], extensionPoint[3];
    double targetRadius = 0.0;

    if (this->AdaptiveExtensionRadius)
      {
      double barycenterToPoint[3];
      double outOfPlaneDistance, projectedDistanceToBarycenter;
      double projectedBarycenterToPoint[3];
      for (j=0; j<numberOfBoundaryPoints; j++)
        {
        boundary->GetPoints()->GetPoint(j,point);
        for (k=0; k<3; k++)
          {
          barycenterToPoint[k] = point[k] - barycenter[k];
          }
        outOfPlaneDistance = vtkMath::Dot(barycenterToPoint,flowExtensionNormal);
        for (k=0; k<3; k++)
          {
          projectedBarycenterToPoint[k] = barycenterToPoint[k] - outOfPlaneDistance*flowExtensionNormal[k];
          }
        targetRadius += vtkMath::Norm(projectedBarycenterToPoint);
        }
      targetRadius /= numberOfBoundaryPoints;
      }
    else
      {
      targetRadius = this->ExtensionRadius;
      }

    vtkIdList* newBoundaryIds = vtkIdList::New();
    vtkIdList* previousBoundaryIds = vtkIdList::New();
    vtkIdType pointId;

    double advancementRatio, factor;

    previousBoundaryIds->DeepCopy(boundaryIds);

    // TODO: use area, not meanRadius as targetRadius

    double targetDistanceBetweenPoints = 2.0 * sin (vtkMath::Pi() / this->NumberOfBoundaryPoints) * targetRadius;

    double currentLength = 0.0;

    vtkThinPlateSplineTransform* thinPlateSplineTransform = vtkThinPlateSplineTransform::New();
    thinPlateSplineTransform->SetBasisToR2LogR();
    thinPlateSplineTransform->SetSigma(this->Sigma);
    
    vtkPoints* sourceLandmarks = vtkPoints::New();
    vtkPoints* targetLandmarks = vtkPoints::New();

    vtkPoints* targetBoundaryPoints = vtkPoints::New();
    vtkPoints* targetStaggeredBoundaryPoints = vtkPoints::New();

    double baseRadialNormal[3];
    boundary->GetPoints()->GetPoint(0,point);
    for (k=0; k<3; k++)
      {
      baseRadialNormal[k] = point[k] - barycenter[k];
      }
    double outOfPlaneComponent = vtkMath::Dot(baseRadialNormal,flowExtensionNormal);
    for (k=0; k<3; k++)
      {
      baseRadialNormal[k] -= outOfPlaneComponent * flowExtensionNormal[k];
      }
    vtkMath::Normalize(baseRadialNormal);
    double angle = 360.0 / numberOfBoundaryPoints;
    if (boundaryDirection == -1)
      {
      angle *= -1.0;
      }
    vtkTransform* transform = vtkTransform::New();
    transform->RotateWXYZ(0.5*angle,flowExtensionNormal);
    double radialVector[3];
    for (k=0; k<3; k++)
      {
      radialVector[k] = targetRadius * baseRadialNormal[k];
      }
    double targetPoint[3];
    for (j=0; j<numberOfBoundaryPoints; j++)
      {
      for (k=0; k<3; k++)
        {
        targetPoint[k] = barycenter[k] + radialVector[k];
        }
      targetBoundaryPoints->InsertNextPoint(targetPoint);
      transform->TransformPoint(radialVector,radialVector);
      for (k=0; k<3; k++)
        {
        targetPoint[k] = barycenter[k] + radialVector[k];
        }
      targetStaggeredBoundaryPoints->InsertNextPoint(targetPoint);
      transform->TransformPoint(radialVector,radialVector);
      }
    transform->Delete();
    
    if (this->InterpolationMode == USE_THIN_PLATE_SPLINE_INTERPOLATION)
      {
      for (j=0; j<numberOfBoundaryPoints; j++)
        {
        double firstBoundaryPoint[3], lastBoundaryPoint[3];
        boundary->GetPoints()->GetPoint(j,point);
        targetBoundaryPoints->GetPoint(j,firstBoundaryPoint);
        sourceLandmarks->InsertNextPoint(firstBoundaryPoint);
        targetLandmarks->InsertNextPoint(point);
        for (k=0; k<3; k++)
          { 
          lastBoundaryPoint[k] = firstBoundaryPoint[k] + extensionLength * this->TransitionRatio * flowExtensionNormal[k]; 
          }
        sourceLandmarks->InsertNextPoint(lastBoundaryPoint);
        targetLandmarks->InsertNextPoint(lastBoundaryPoint);
        }
      thinPlateSplineTransform->SetSourceLandmarks(sourceLandmarks);
      thinPlateSplineTransform->SetTargetLandmarks(targetLandmarks);
      }

//    if (boundaryDirection == -1)
//      {
//      previousBoundaryIds->Initialize();
//      for (j=0; j<numberOfBoundaryPoints; j++)
//        {
//        previousBoundaryIds->InsertNextId(boundaryIds->GetId(numberOfBoundaryPoints-j-1));
//        }
//      }

    int numberOfLayers = extensionLength / targetDistanceBetweenPoints;
    int numberOfTransitionLayers = (extensionLength * this->TransitionRatio) / targetDistanceBetweenPoints;
    int l;
    for (l=0; l<numberOfLayers; l++)
      {
      newBoundaryIds->Initialize();
      for (j=0; j<numberOfBoundaryPoints; j++)
        {
        if (l%2 != 0)
          {
          targetBoundaryPoints->GetPoint(j,extensionPoint);
          }
        else
          {
          targetStaggeredBoundaryPoints->GetPoint(j,extensionPoint);
          }
        for (k=0; k<3; k++)
          {
          extensionPoint[k] += (l+1) * targetDistanceBetweenPoints * flowExtensionNormal[k];
          }
        if (l<numberOfTransitionLayers)
          {
          if (this->InterpolationMode == USE_LINEAR_INTERPOLATION)
            {
            }
          else if (this->InterpolationMode == USE_THIN_PLATE_SPLINE_INTERPOLATION)
            {
            thinPlateSplineTransform->TransformPoint(extensionPoint,extensionPoint);
            }
          }
        pointId = outputPoints->InsertNextPoint(extensionPoint);
        newBoundaryIds->InsertNextId(pointId);
        }

      vtkIdType pts[3];
      for (j=0; j<numberOfBoundaryPoints; j++)
        {
        if (l%2 != 0)
          {
          pts[0] = newBoundaryIds->GetId(j);
          pts[1] = newBoundaryIds->GetId((j-1+numberOfBoundaryPoints)%numberOfBoundaryPoints);
          pts[2] = previousBoundaryIds->GetId((j-1+numberOfBoundaryPoints)%numberOfBoundaryPoints);
          outputPolys->InsertNextCell(3,pts);

          pts[0] = previousBoundaryIds->GetId(j);
          pts[1] = newBoundaryIds->GetId(j);
          pts[2] = previousBoundaryIds->GetId((j-1+numberOfBoundaryPoints)%numberOfBoundaryPoints);
          outputPolys->InsertNextCell(3,pts);
          }
        else
          {
          pts[0] = newBoundaryIds->GetId(j);
          pts[1] = newBoundaryIds->GetId((j-1+numberOfBoundaryPoints)%numberOfBoundaryPoints);
          pts[2] = previousBoundaryIds->GetId(j);
          outputPolys->InsertNextCell(3,pts);
  
          pts[0] = previousBoundaryIds->GetId(j);
          pts[1] = newBoundaryIds->GetId((j-1+numberOfBoundaryPoints)%numberOfBoundaryPoints);
          pts[2] = previousBoundaryIds->GetId((j-1+numberOfBoundaryPoints)%numberOfBoundaryPoints);
          outputPolys->InsertNextCell(3,pts);
          }
        }

      previousBoundaryIds->DeepCopy(newBoundaryIds);
      }

    targetBoundaryPoints->Delete();
    targetStaggeredBoundaryPoints->Delete();
    newBoundaryIds->Delete();
    previousBoundaryIds->Delete();
    boundaryIds->Delete();
    thinPlateSplineTransform->Delete();
    sourceLandmarks->Delete();
    targetLandmarks->Delete();
    }

  output->SetPoints(outputPoints);
  output->SetPolys(outputPolys);

  outputPoints->Delete();
  outputPolys->Delete();

  tube->Delete();
  zeroRadiusArray->Delete();
  boundaryExtractor->Delete();

  return 1;
}

void vtkvmtkPolyDataFlowExtensionsFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
}
