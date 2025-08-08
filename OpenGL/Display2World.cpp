void Display2World::DrawPolyline()
{
  return;
  // 获取3D渲染窗口
  auto win3d = this->GetRenderWindowPart()->GetQmitkRenderWindow("3d");
  win3d->update();
  QPainter painter(win3d);

  {
      // 设置画笔颜色为红色
      painter.setPen(QColor(255, 0, 0));
      QVector<QLineF> lines;
      // 遍历多边形点，绘制多边形
      for (int i = 0; i < m_PolylinePoints.size() - 1; i++)
      {
          QLineF line(QPointF(m_PolylinePoints[i].x(), m_PolylinePoints[i].y()), QPointF(m_PolylinePoints[i + 1].x(), m_PolylinePoints[i + 1].y()));
          lines.push_back(line);
      }
      // 如果多边形点不为空，绘制最后一条线段
      if (m_PolylinePoints.size() > 0)
      {
          QLineF line(QPointF(m_PolylinePoints[m_PolylinePoints.size() - 1].x(), m_PolylinePoints[m_PolylinePoints.size() - 1].y()), m_StartScalpelPosition);
          lines.push_back(line);
      }
      // 绘制多边形
      painter.drawLines(lines);
  }
  
}

void Display2World::UpdateScalpelRegion()
{
  auto selectedNode = m_SelectedNode.Lock();

  if (selectedNode.IsNull())
  {
    return;
  }
  auto win3d = this->GetRenderWindowPart()->GetQmitkRenderWindow("3d");
  QVector<QPointF> region = win3d->GetPolylinePoints();
  if (region.size() < 3)
  {
    MITK_WARN << "Scalpel region must have at least 3 points.";
    return;
  }

  float xmin, xmax, ymin, ymax;
  xmin = xmax = region[0].x();
  ymin = ymax = region[0].y();

  for (int i = 1; i < region.size(); i++)
  {
    if (region[i].x() < xmin)
      xmin = region[i].x();
    if (region[i].x() > xmax)
      xmax = region[i].x();
    if (region[i].y() < ymin)
      ymin = region[i].y();
    if (region[i].y() > ymax)
      ymax = region[i].y();
  }

  auto image = dynamic_cast<mitk::Image *>(selectedNode->GetData());
  auto volume = image->GetVtkImageData();

  double spacing[3];
  volume->GetSpacing(spacing);

  double origin[3];
  volume->GetOrigin(origin);

  int extent[6];
  volume->GetExtent(extent);

  double worldX, worldY, worldZ;
  auto renderer = win3d->GetRenderer()->GetVtkRenderer();
  vtkCamera *camera = renderer->GetActiveCamera();
  vtkTransform *rigTranform = renderer->GetCameraRig()->GetTransform();
  vtkSmartPointer<vtkTransform> rigTranformInv = vtkSmartPointer<vtkTransform>::New();
  rigTranformInv->DeepCopy(rigTranform);
  rigTranformInv->Inverse();

  // float DevicePixelRatio = 1.0;
  // QScreen *screen = QGuiApplication::primaryScreen();
  // if (screen)
  // {
  //     DevicePixelRatio = screen->devicePixelRatio();
  // }

  // float logicalWidth = win3d->GetVtkRenderWindow()->GetSize()[0] / DevicePixelRatio;
  // float logicalHeight = win3d->GetVtkRenderWindow()->GetSize()[1] / DevicePixelRatio;

  // double actualWidth = renderer->GetCameraRig()->GetActualWidth();
  // double actualHeight = renderer->GetCameraRig()->GetActualHeight();

  double worldpos[4];
  double displayPos[4];

  vtkRenderWindow *renWin = renderer->GetRenderWindow();
  int *vtkSize = renWin->GetSize();
  int windowHeight = vtkSize[1];

  float actualscale = 1.0;
  QPolygonF polygon(region);

  vtkNew<vtkCoordinate> coord;
  coord->SetCoordinateSystemToWorld();
#if 0
  mitk::Point3D index;
  mitk::Point3D world;
  for (int z = extent[4]; z <= extent[5]; z++)
    for (int y = extent[2]; y <= extent[3]; y++)
      for (int x = extent[0]; x <= extent[1]; x++)
      {
        index[0] = x;
        index[1] = y;
        index[2] = z;
        image->GetGeometry()->IndexToWorld(index, world);

        worldpos[0] = world[0];
        worldpos[1] = world[1];
        worldpos[2] = world[2];
        worldpos[3] = 1.0;

        renderer->SetWorldPoint(worldpos[0], worldpos[1], worldpos[2], worldpos[3]);
        renderer->WorldToDisplay();
        renderer->GetDisplayPoint(displayPos);

        // rigTranformInv->MultiplyPoint(worldpos, displayPos);

        // displayPos[0] = (displayPos[0]) / actualWidth * actualscale;
        // displayPos[1] = (displayPos[1]) / actualHeight * actualscale;

        // displayPos[0] = (displayPos[0]+1)/2.0 * logicalWidth;
        // displayPos[1] = (displayPos[1]+1)/2.0 * logicalHeight;

        displayPos[1] = windowHeight - displayPos[1] - 1; // Y轴翻转

        if (polygon.containsPoint(QPointF(displayPos[0], displayPos[1]), Qt::OddEvenFill))
        {
          volume->SetScalarComponentFromDouble(x, y, z, 0, -3000.0); // 设置体素值为-3000.0
        }
      }

  // volume->Modified();
  // image->Modified();
  // selectedNode->Modified();
#else
  GenROIMesh();
#endif
  volume->Modified();
  image->Modified();
  selectedNode->Modified();
  mitk::RenderingManager::GetInstance()->RequestUpdateAll();
}
void Display2World::OnCropData(const itk::Object *caller, const itk::EventObject &event)
{
  mitk::DataNode::ConstPointer cropNode = dynamic_cast<const mitk::DataNode *>(caller);
  if (cropNode.IsNull())
    return;

  auto selectedNode = m_SelectedNode.Lock();

  if (selectedNode.IsNull())
  {
    return;
  }
  if (nullptr == selectedNode->GetData())
  {
    return;
  }
  if (selectedNode->IsVisible(nullptr) == false)
  {
    return;
  }

  auto *geometryData = dynamic_cast<mitk::GeometryData *>(cropNode->GetData());
  if (geometryData == nullptr)
    return;

  mitk::BaseGeometry::Pointer geometry = geometryData->GetGeometry();

  if (geometry == nullptr)
    mitkThrow() << "Geometry is not valid.";

  mitk::Vector3D spacing = geometry->GetSpacing();

  mitk::BoundingBox::ConstPointer boundingBox = geometry->GetBoundingBox();
  mitk::Point3D BBmin = boundingBox->GetMinimum();
  mitk::Point3D BBmax = boundingBox->GetMaximum();

  auto renderWindowPart = this->GetRenderWindowPart(mitk::WorkbenchUtil::IRenderWindowPartStrategy::OPEN);
  const mitk::TimePointType timePoint = renderWindowPart->GetSelectedTimePoint();
  const auto imageGeometry = selectedNode->GetData()->GetTimeGeometry()->GetGeometryForTimePoint(timePoint);

  mitk::Point3D init_ofi = imageGeometry->GetOrigin();
  mitk::Point3D geo_ori = geometry->GetOrigin();
  double offset[3] = {init_ofi[0] - geo_ori[0], init_ofi[1] - geo_ori[1], init_ofi[2] - geo_ori[2]};
  double epsilon = 0;
  std::vector<double> regvec = {static_cast<double>(BBmin[0] - offset[0] / spacing[0] + epsilon),
                                static_cast<double>(BBmax[0] - offset[0] / spacing[0] - 1),
                                static_cast<double>(BBmin[1] - offset[1] / spacing[1] + epsilon),
                                static_cast<double>(BBmax[1] - offset[1] / spacing[1] - 1),
                                static_cast<double>(BBmin[2] - offset[2] / spacing[2] + epsilon),
                                static_cast<double>(BBmax[2] - offset[2] / spacing[2] - 1)};
  auto region = mitk::VectorProperty<double>::New();
  region->SetValue(regvec);
  selectedNode->SetProperty("crop region", region);
  selectedNode->Modified();
}

void Display2World::OnEnableCrop(bool state)
{
  itk::MemberCommand<Display2World>::Pointer dataModifiedCommand;
  dataModifiedCommand = itk::MemberCommand<Display2World>::New();
  dataModifiedCommand->SetCallbackFunction(this, &Display2World::OnCropData);

  auto selectedNode = m_SelectedNode.Lock();

  if (selectedNode.IsNull())
  {
    return;
  }
  if (nullptr == selectedNode->GetData())
  {
    return;
  }
  if (selectedNode->IsVisible(nullptr) == false)
  {
    MITK_INFO << "Node is not visible. Cannot create crop box.";
    m_Controls->enableCrop->setChecked(false);
    return;
  }

  QString name = QString::fromStdString(selectedNode->GetName() + "_CropBox");

  selectedNode->SetProperty("enable crop", mitk::BoolProperty::New(state));

  if (state)
  {
    auto renderWindowPart = this->GetRenderWindowPart(mitk::WorkbenchUtil::IRenderWindowPartStrategy::OPEN);
    const mitk::TimePointType timePoint = renderWindowPart->GetSelectedTimePoint();
    const auto imageGeometry = selectedNode->GetData()->GetTimeGeometry()->GetGeometryForTimePoint(timePoint);

    if (imageGeometry == nullptr)
      mitkThrow() << "Geometry is not valid.";

    auto boundingGeometry = mitk::Geometry3D::New();
    boundingGeometry->SetBounds(imageGeometry->GetBounds());
    boundingGeometry->SetImageGeometry(imageGeometry->GetImageGeometry());
    boundingGeometry->SetOrigin(imageGeometry->GetOrigin());
    boundingGeometry->SetSpacing(imageGeometry->GetSpacing());
    boundingGeometry->SetIndexToWorldTransform(imageGeometry->GetIndexToWorldTransform()->Clone());
    boundingGeometry->Modified();

    auto boundingBox = mitk::GeometryData::New();
    boundingBox->SetGeometry(boundingGeometry);
    auto boundingBoxNode = mitk::DataNode::New();
    boundingBoxNode->SetData(boundingBox);
    boundingBoxNode->SetProperty("name", mitk::StringProperty::New(name.toStdString()));
    boundingBoxNode->SetProperty("layer", mitk::IntProperty::New(99));
    boundingBoxNode->AddProperty("Bounding Shape.Handle Size Factor", mitk::DoubleProperty::New(0.02));
    boundingBoxNode->SetBoolProperty("pickable", true);

    GetDataStorage()->Add(boundingBoxNode, selectedNode);

    m_CropChangedObserverTag = boundingBoxNode->AddObserver(itk::ModifiedEvent(), dataModifiedCommand);

    mitk::BoundingShapeInteractor::Pointer interactor = mitk::BoundingShapeInteractor::New();
    interactor->LoadStateMachine("BoundingShapeInteraction.xml", us::ModuleRegistry::GetModule("MitkBoundingShape"));
    interactor->SetEventConfig("BoundingShapeMouseConfig.xml", us::ModuleRegistry::GetModule("MitkBoundingShape"));
    interactor->SetDataNode(boundingBoxNode);
  }
  else
  {
    // remove crop box
    mitk::DataNode::Pointer cropBoxNode = this->GetDataStorage()->GetNamedNode(name.toStdString());
    if (cropBoxNode.IsNotNull())
    {
      cropBoxNode->SetDataInteractor(nullptr);
      cropBoxNode->RemoveObserver(m_CropChangedObserverTag);
      this->GetDataStorage()->Remove(cropBoxNode);
    }
  }
  mitk::RenderingManager::GetInstance()->RequestUpdateAll();
  return;
}

int count = 0;
void Display2World::GenROIMesh()
{
  auto selectedNode = m_SelectedNode.Lock();

  if (selectedNode.IsNull())
  {
    return;
  }
  auto win3d = this->GetRenderWindowPart()->GetQmitkRenderWindow("3d");
  QVector<QPointF> region = win3d->GetPolylinePoints();
  if (region.size() < 3)
  {
    MITK_WARN << "Scalpel region must have at least 3 points.";
    return;
  }

  auto image = dynamic_cast<mitk::Image *>(selectedNode->GetData());
  auto volume = image->GetVtkImageData();

  

  double spacing[3];
  volume->GetSpacing(spacing);
  MITK_INFO<<"spacing: "<<spacing[0]<<", "<<spacing[1]<<", "<<spacing[2];

  double origin[3];
  volume->GetOrigin(origin);
  MITK_INFO<<"origin: "<<origin[0]<<", "<<origin[1]<<", "<<origin[2];

  int extent[6];
  volume->GetExtent(extent);

  double worldX, worldY, worldZ;
  auto renderer = win3d->GetRenderer()->GetVtkRenderer();
  vtkCameraRig* rig = renderer->GetCameraRig();
  vtkCamera *camera = renderer->GetActiveCamera();
  vtkTransform *rigTranform = renderer->GetCameraRig()->GetTransform();
  vtkSmartPointer<vtkTransform> rigTranformInv = vtkSmartPointer<vtkTransform>::New();
  rigTranformInv->DeepCopy(rigTranform);
  rigTranformInv->Inverse();

    // Camera parameters
    // Camera position
    double cameraPos[4] = { 0 };
    // camera->GetPosition(cameraPos);
    camera->GetLeftEyePosition(cameraPos);
    // auto rig_pos = rig->GetRigPosition();
    // for(int i = 0; i < 3; i++)
    // {
    //   cameraPos[i] = rig_pos[i];
    // }
    cameraPos[3] = 1.0;
    // Focal point position
    double cameraFP[4] = { 0 };
    // camera->GetFocalPoint(cameraFP);
    auto rig_fp = rig->GetFocalPoint();
    for(int i = 0; i < 3; i++)
    {
      cameraFP[i] = rig_fp[i];
    }
    cameraFP[3] = 1.0;
    // Direction of projection
    double cameraDOP[3] = { 0 };
    for (int i = 0; i < 3; i++)
    {
      cameraDOP[i] = cameraFP[i] - cameraPos[i];
    }
    vtkMath::Normalize(cameraDOP);
    // Camera view up
    double cameraViewUp[3] = { 0 };
    camera->GetViewUp(cameraViewUp);
    vtkMath::Normalize(cameraViewUp);

    renderer->SetWorldPoint(cameraFP[0], cameraFP[1], cameraFP[2], cameraFP[3]);
    renderer->WorldToDisplay();
    double* displayCoords = renderer->GetDisplayPoint();
    double selectionZ = displayCoords[2];

    // Clip what we see on the camera but reduce it to the modifier labelmap's range
    // to keep the stencil as small as possible
    double* clipRange = camera->GetClippingRange();

    vtkNew<vtkPoints> closedSurfacePoints; // p0Top, p0Bottom, p1Top, p1Bottom, ...

    

    int numberOfPoints = region.size();

    vtkRenderWindow *renWin = renderer->GetRenderWindow();
    int *vtkSize = renWin->GetSize();
    int windowHeight = vtkSize[1];

    MITK_INFO<<"vtkSize: "<<vtkSize[0]<<", "<<vtkSize[1];

    float dpr = win3d->devicePixelRatioF();
    MITK_INFO<<"dpr: "<<dpr;

    QSize s = win3d->size();
    MITK_INFO<<"window size: "<<s.width()<<", "<<s.height();

    {
      double actualWidth = renderer->GetCameraRig()->GetActualWidth();
      double actualHeight = renderer->GetCameraRig()->GetActualHeight();


      MITK_INFO<<"actualWidth: "<<actualWidth<<", actualHeight: "<<actualHeight;
    }



    // {
    //   int x[3] = {0,0,0};
    //   int y[3] = {511,0,0};
    //   int z[3] = {0,0,501};
      
    //   region.clear();

    //   mitk::Point3D index;
    //   mitk::Point3D world;
      
    //   {
    //     index[0] = x[0];
    //     index[1] = x[1];
    //     index[2] = x[2];
    //     image->GetGeometry()->IndexToWorld(index, world);
    //     renderer->SetWorldPoint(world[0], world[1], world[2], 1.0);
    //     renderer->WorldToDisplay();
    //     double* displayCoords = renderer->GetDisplayPoint();
    //     region.push_back(QPointF(displayCoords[0], windowHeight - displayCoords[1] - 1));

    //     MITK_INFO<<"displayCoords: "<<displayCoords[0]<<", "<< windowHeight - displayCoords[1] - 1<<", "<<displayCoords[2];

    //     vtkNew<vtkSphereSource> sphereSource;
    //     sphereSource->SetCenter(world[0], world[1], world[2]);
    //     sphereSource->SetRadius(50.0);
    //     sphereSource->Update();

    //     vtkNew<vtkPolyDataMapper> mapper;
    //     mapper->SetInputConnection(sphereSource->GetOutputPort());
    //     vtkNew<vtkActor> scalpelActorNormal;
    //     scalpelActorNormal->SetMapper(mapper);
    //     scalpelActorNormal->GetProperty()->SetOpacity(0.5);
    //     scalpelActorNormal->GetProperty()->SetColor(1.0, 0.0, 0.0);
    //     renderer->AddActor(scalpelActorNormal);
    //   }

    //   {
    //     index[0] = y[0];
    //     index[1] = y[1];
    //     index[2] = y[2];
    //     image->GetGeometry()->IndexToWorld(index, world);
    //     renderer->SetWorldPoint(world[0], world[1], world[2], 1.0);
    //     renderer->WorldToDisplay();
    //     double* displayCoords = renderer->GetDisplayPoint();
    //     region.push_back(QPointF(displayCoords[0], windowHeight- 1- displayCoords[1]));

    //     MITK_INFO<<"displayCoords: "<<displayCoords[0]<<", "<< windowHeight - displayCoords[1] - 1<<", "<<displayCoords[2];

    //     vtkNew<vtkSphereSource> sphereSource;
    //     sphereSource->SetCenter(world[0], world[1], world[2]);
    //     sphereSource->SetRadius(50.0);
    //     sphereSource->Update();

    //     vtkNew<vtkPolyDataMapper> mapper;
    //     mapper->SetInputConnection(sphereSource->GetOutputPort());
    //     vtkNew<vtkActor> scalpelActorNormal;
    //     scalpelActorNormal->SetMapper(mapper);
    //     scalpelActorNormal->GetProperty()->SetOpacity(0.5);
    //     scalpelActorNormal->GetProperty()->SetColor(0.0, 1.0, 0.0);
    //     renderer->AddActor(scalpelActorNormal);
    //   }

    //   {
    //     index[0] = z[0];
    //     index[1] = z[1];
    //     index[2] = z[2];
    //     image->GetGeometry()->IndexToWorld(index, world);
    //     renderer->SetWorldPoint(world[0], world[1], world[2], 1.0);
    //     renderer->WorldToDisplay();
    //     double* displayCoords = renderer->GetDisplayPoint();
    //     region.push_back(QPointF(displayCoords[0], windowHeight- 1- displayCoords[1]));

    //     MITK_INFO<<"displayCoords: "<<displayCoords[0]<<", "<< windowHeight - displayCoords[1] - 1<<", "<<displayCoords[2];

    //     vtkNew<vtkSphereSource> sphereSource;
    //     sphereSource->SetCenter(world[0], world[1], world[2]);
    //     sphereSource->SetRadius(50.0);
    //     sphereSource->Update();

    //     vtkNew<vtkPolyDataMapper> mapper;
    //     mapper->SetInputConnection(sphereSource->GetOutputPort());
    //     vtkNew<vtkActor> scalpelActorNormal;
    //     scalpelActorNormal->SetMapper(mapper);
    //     scalpelActorNormal->GetProperty()->SetOpacity(0.5);
    //     scalpelActorNormal->GetProperty()->SetColor(0.0, 0.0, 1.0);
    //     renderer->AddActor(scalpelActorNormal);
    //   }
      
    // }

    MITK_INFO<<"cameraPos: "<<cameraPos[0]<<", "<<cameraPos[1]<<", "<<cameraPos[2];
    MITK_INFO<<"cameraFP: "<<cameraFP[0]<<", "<<cameraFP[1]<<", "<<cameraFP[2];
    MITK_INFO<<"cameraDOP: "<<cameraDOP[0]<<", "<<cameraDOP[1]<<", "<<cameraDOP[2];
    MITK_INFO<<"clipRange: "<<clipRange[0]<<", "<<clipRange[1];

    bool binocular = sy::GetDisplayMode()==4?true:false;

    for (int pointIndex = 0; pointIndex < numberOfPoints; pointIndex++)
    {
      vtkRenderer *ren = renderer;
      vtkCamera *camera = ren->GetActiveCamera();
      int logicalWidth = vtkSize[0]/dpr;//GetLogicalWidth();
      int logicalHeight = vtkSize[1]/dpr;//GetLogicalHeight();

      double in[3] = { region[pointIndex].x(), windowHeight/dpr - region[pointIndex].y() - 1, 0. };
    
      // Normalize and center
      double mouseDisplayPos[4];
      // if (binocular)
      // {
      //   mouseDisplayPos[0] = 2 * in[0] / (0.5 * logicalWidth) - 1.0;
      // }
      // else
      {
        mouseDisplayPos[0] = 2 * in[0] / (1.0 * logicalWidth) - 1.0;
      }
      mouseDisplayPos[1] = 2 * in[1] / logicalHeight - 1.0;
      mouseDisplayPos[2] = 0.0;
      mouseDisplayPos[3] = 1;
    
      double actualWidth = ren->GetCameraRig()->GetActualWidth();
      double actualHeight = ren->GetCameraRig()->GetActualHeight();
    
      double m_EyebrowsDistance = 500;
      // Actual mouse point physical coordinates based on center; currently the point is on screen
      mouseDisplayPos[0] *= (0.5 * actualWidth);
      mouseDisplayPos[1] *= (0.5 * actualHeight);
      mouseDisplayPos[2] = -m_EyebrowsDistance;
    
      double origin[3] = {0, 0, 0};
      double direction[3];
      std::string eyeType = "";
      if (eyeType == "left" || eyeType == "right" || eyeType == "center")
      {
        double eyePosition[4], eyePositionDisplay[4];
        if (eyeType == "left")
        {
          ren->GetCameraRig()->GetCamera()->GetLeftEyePosition(eyePosition);
        }
        else if (eyeType == "right")
        {
          ren->GetCameraRig()->GetCamera()->GetRightEyePosition(eyePosition);
        }
        else
        {
          ren->GetCameraRig()->GetCamera()->GetEyePosition(eyePosition);
        }    
        eyePosition[3] = 1.0;
        vtkSmartPointer<vtkTransform> rigTranformInv = vtkSmartPointer<vtkTransform>::New();
        rigTranformInv->DeepCopy(ren->GetCameraRig()->GetTransform());
        rigTranformInv->Inverse();
        rigTranformInv->MultiplyPoint(eyePosition, eyePositionDisplay);
        for (size_t i = 0; i < 3; i++)
        {
          origin[i] = eyePositionDisplay[i];
        }
      }
      
      double dof = win3d->GetDOF();
      vtkMath::Subtract(mouseDisplayPos, origin, direction);
      vtkMath::Normalize(direction);
      for (int i = 0; i < 3; ++i)
      {
        mouseDisplayPos[i] = mouseDisplayPos[i] + direction[i] * dof;
      }
    
      // if (outputDirection != nullptr)
      // {
      //   outputDirection[0] = direction[0];
      //   outputDirection[1] = direction[1];
      //   outputDirection[2] = direction[2];
      // }
      double worldCoords[4] = { 0 };
      vtkTransform *rigTranform = ren->GetCameraRig()->GetTransform();
      rigTranform->MultiplyPoint(mouseDisplayPos, worldCoords);
      worldCoords[3] = 1.0;

      
      // // Convert the selection point into world coordinates.
      // double pointXY[3] = { region[pointIndex].x()*dpr, windowHeight - region[pointIndex].y()*dpr - 1, 0. };
      // renderer->SetDisplayPoint(pointXY[0], pointXY[1], selectionZ);
      // renderer->DisplayToWorld();
      // double* worldCoords = renderer->GetWorldPoint();

      if (worldCoords[3] == 0.0)
      {
        qWarning() << Q_FUNC_INFO << ": Bad homogeneous coordinates";
        return ;
      }
      // MITK_INFO<< "worldCoords: " << worldCoords[0] << ", " << worldCoords[1] << ", " << worldCoords[2] << ", " << worldCoords[3];
      double pickPosition[3] = { 0 };
      for (int i = 0; i < 3; i++)
      {
        pickPosition[i] = worldCoords[i] / worldCoords[3];
      }

      //  Compute the ray endpoints.  The ray is along the line running from
      //  the camera position to the selection point, starting where this line
      //  intersects the front clipping plane, and terminating where this
      //  line intersects the back clipping plane.
      double ray[3] = { 0 };
      for (int i = 0; i < 3; i++)
      {
        ray[i] = pickPosition[i] - cameraPos[i];
      }

      // MITK_INFO<<"ray: "<<ray[0]<<", "<<ray[1]<<", "<<ray[2];

      double rayLength = vtkMath::Dot(cameraDOP, ray);
      if (rayLength == 0.0)
      {
        qWarning() << Q_FUNC_INFO << ": Cannot process points";
        return ;
      }

      // MITK_INFO<<"rayLength: "<<rayLength;

      double p1World[4] = { 0 };
      double p2World[4] = { 0 };
      double tF = 0;
      double tB = 0;
      if (camera->GetParallelProjection())
      {
        tF = clipRange[0] - rayLength;
        tB = clipRange[1] - rayLength;
        for (int i = 0; i < 3; i++)
        {
          p1World[i] = pickPosition[i] + tF*cameraDOP[i];
          p2World[i] = pickPosition[i] + tB*cameraDOP[i];
        }
      }
      else
      {
        tF = clipRange[0] / rayLength;
        tB = clipRange[1] / rayLength;
        for (int i = 0; i < 3; i++)
        {
          p1World[i] = cameraPos[i] + tF*ray[i];
          p2World[i] = cameraPos[i] + tB*ray[i];
        }
      }
      p1World[3] = p2World[3] = 1.0;

      closedSurfacePoints->InsertNextPoint(p1World);
      closedSurfacePoints->InsertNextPoint(p2World);
    }

    // Skirt
  vtkNew<vtkCellArray> closedSurfacePolys;
  vtkNew<vtkCellArray> closedSurfaceStrips;
  closedSurfaceStrips->InsertNextCell(numberOfPoints * 2 + 2);
  for (int i = 0; i < numberOfPoints * 2; i++)
  {
    closedSurfaceStrips->InsertCellPoint(i);
  }
  closedSurfaceStrips->InsertCellPoint(0);
  closedSurfaceStrips->InsertCellPoint(1);
  // Front cap
  closedSurfacePolys->InsertNextCell(numberOfPoints);
  for (int i = 0; i < numberOfPoints; i++)
  {
    closedSurfacePolys->InsertCellPoint(i * 2);
  }
  // Back cap
  closedSurfacePolys->InsertNextCell(numberOfPoints);
  for (int i = 0; i < numberOfPoints; i++)
  {
    closedSurfacePolys->InsertCellPoint(i * 2 + 1);
  }

  // Construct polydata
  vtkNew<vtkPolyData> closedSurfacePolyData;
  closedSurfacePolyData->SetPoints(closedSurfacePoints.GetPointer());
  closedSurfacePolyData->SetStrips(closedSurfaceStrips.GetPointer());
  closedSurfacePolyData->SetPolys(closedSurfacePolys.GetPointer());

  vtkNew<vtkPolyDataNormals> normals;
  normals->SetInputData(closedSurfacePolyData);
  normals->AutoOrientNormalsOn();
  normals->Update();

  // auto mat = renderer->GetViewTransformMatrix();
  auto mat = rigTranform->GetMatrix();
  // double matrix[16];
  // for (int i = 0; i < 16; i++)
  // {
  //   matrix[i] = static_cast<float>(mat[i]);
  // }
  vtkNew<vtkMatrix4x4> mat_offset;
  image->GetGeometry()->GetVtkTransform()->GetMatrix(mat_offset);

  // vtkNew<vtkMatrix4x4> viewMatrix;
  // viewMatrix->Identity();
  // for (int i = 0; i < 3; i++)
  //   {
  //     // viewMatrix->SetElement(i, 3, mat->Element[i][3]);
  //     // viewMatrix->SetElement(i, 0, mat->Element[i][1]);
  //     // viewMatrix->SetElement(i, 1, mat->Element[i][0]);
  //     // viewMatrix->SetElement(i, 2, mat->Element[i][2]);
  //     // viewMatrix->SetElement(3, i, mat->Element[3][i]);
  //     // viewMatrix->SetElement(0, i, mat->Element[1][i]);
  //     // viewMatrix->SetElement(1, i, mat->Element[0][i]);
  //     // viewMatrix->SetElement(2, i, mat->Element[2][i]);
  //     // viewMatrix->SetElement(i, 3, mat->Element[i][3]);
  //     // viewMatrix->SetElement(i, 0, mat->Element[i][1]);
  //     // viewMatrix->SetElement(i, 1, mat->Element[i][0]);
  //     // viewMatrix->SetElement(i, 2, mat->Element[i][2]);

  //     viewMatrix->SetElement(i, 3, mat->Element[i][3]);
  //     viewMatrix->SetElement(i, 0, mat->Element[i][0]);
  //     viewMatrix->SetElement(i, 1, mat->Element[i][1]);
  //     viewMatrix->SetElement(i, 2, mat->Element[i][2]);
  //   }

  vtkNew<vtkTransform> transform;
  transform->Identity();
  // transform->RotateZ(-180.0); // Rotate around Z-axis to match the camera view
  transform->Translate(mat_offset->Element[0][3], mat_offset->Element[1][3], mat_offset->Element[2][3]);
  // transform->SetMatrix(mat);
  // transform->RotateWXYZ(180.0, 0, 0, 1); // Rotate around Z-axis to match the camera view
  // transform->SetMatrix(mat_offset);
  transform->Inverse();
  // transform->Translate(origin[0], origin[1], origin[2]);
  
  // transform->Translate(-mat_offset->Element[0][3], -mat_offset->Element[1][3], -mat_offset->Element[2][3]);
  transform->Update();

  vtkNew<vtkTransformPolyDataFilter> transformFilter;
  transformFilter->SetInputConnection(normals->GetOutputPort()); 
  transformFilter->SetTransform(transform);
  transformFilter->Update();

  // vtkNew<vtkPolyDataMapper> mapper;
  // mapper->SetInputConnection(transformFilter->GetOutputPort());
  // vtkNew<vtkActor> scalpelActor;
  // scalpelActor->SetMapper(mapper);
  // scalpelActor->GetProperty()->SetOpacity(0.5);
  // scalpelActor->GetProperty()->SetColor(1.0, 0.0, 0.0);
  // renderer->AddActor(scalpelActor);

  {
    vtkNew<vtkPolyDataMapper> mapper;
  mapper->SetInputConnection(normals->GetOutputPort());
  // mapper->SetInputConnection(transformFilter->GetOutputPort());
  vtkNew<vtkActor> scalpelActorNormal;
  scalpelActorNormal->SetMapper(mapper);
  scalpelActorNormal->GetProperty()->SetOpacity(0.5);
  scalpelActorNormal->GetProperty()->SetColor(0.0, 1.0, 0.0);
  renderer->AddActor(scalpelActorNormal);
  }

  double extentCut[6];
  transformFilter->GetOutput()->GetBounds(extentCut);
  // normals->GetOutput()->GetBounds(extentCut);

  // for(int i=0;i<6;i++)
  // {
  //   MITK_INFO<<"mesh extentCut["<<i<<"] = "<<extentCut[i];
  // }

  for(int i = 0; i < 3; i++)
  {
    extentCut[2*i] = (extentCut[2*i]-origin[i])/spacing[i];
    extentCut[2*i+1] = (extentCut[2*i+1]-origin[i])/spacing[i];
    // extentCut[2*i] = (extentCut[2*i])/spacing[i];
    // extentCut[2*i+1] = (extentCut[2*i+1])/spacing[i];
    // extentCut[2*i] = (extentCut[2*i]+origin[i])/spacing[i];
    // extentCut[2*i+1] = (extentCut[2*i+1]+origin[i])/spacing[i];
  }

  for(int i=0;i<6;i++)
  {
    MITK_INFO<<"index extentCut["<<i<<"] = "<<extentCut[i];
  }
  
  for(int i=0;i<3;i++)
  {
    extentCut[2*i] = std::max((int)floor(extentCut[2*i]),extent[2*i]);
    extentCut[2*i+1] = std::min((int)ceil(extentCut[2*i+1]),extent[2*i+1]);
  }

  for(int i=0;i<6;i++)
  {
    MITK_INFO<<"rounded index extentCut["<<i<<"] = "<<extentCut[i];
  }


  vtkNew<vtkPolyDataToImageStencil> polyDataToImageStencil;
  polyDataToImageStencil->SetInputConnection(transformFilter->GetOutputPort());
  // polyDataToImageStencil->SetInputConnection(normals->GetOutputPort());
  polyDataToImageStencil->SetOutputSpacing(spacing[0], spacing[1], spacing[2]);
  polyDataToImageStencil->SetOutputWholeExtent(extentCut[0], extentCut[1],
                                               extentCut[2], extentCut[3],
                                               extentCut[4], extentCut[5]);
  polyDataToImageStencil->SetOutputOrigin(origin[0], origin[1], origin[2]);

  // polyDataToImageStencil->GetOutput();

  vtkNew<vtkImageData> stencilImage;
  stencilImage->SetDimensions(extent[1] - extent[0] + 1,
                              extent[3] - extent[2] + 1,
                              extent[5] - extent[4] + 1);
  // stencilImage->SetDimensions(extentCut[1] - extentCut[0] + 1, extentCut[3] - extentCut[2] + 1, extentCut[5] - extentCut[4] + 1);
  stencilImage->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
  stencilImage->SetSpacing(spacing[0], spacing[1], spacing[2]);
  stencilImage->SetOrigin(origin[0], origin[1], origin[2]);
  stencilImage->GetPointData()->GetScalars()->Fill(255);

  vtkNew<vtkImageStencil> stencil;
  stencil->SetInputData(stencilImage);
  stencil->SetStencilConnection(polyDataToImageStencil->GetOutputPort());
  stencil->ReverseStencilOn();
  stencil->SetBackgroundValue(0.0);
  stencil->Update();

  // {
  //   std::string file = "D:\\Workspace\\Data\\test/stenciled_";
  // file += std::to_string(count);
  // file += ".vti";
  // vtkNew<vtkXMLImageDataWriter> writer;
  // writer->SetFileName(file.c_str());
  // writer->SetInputData(stencil->GetOutput());
  // writer->Write();
  // }

  vtkNew<vtkImageMask> mask;
  mask->SetInputData(0, volume);
  mask->SetInputConnection(1, stencil->GetOutputPort());
  mask->SetMaskedOutputValue(-3000.0);
  mask->Update();

  volume->DeepCopy(mask->GetOutput());

//   {
//     std::string file = "D:\\Workspace\\Data\\test/masked_";
//   file += std::to_string(count);
//   file += ".vti";
//   vtkNew<vtkXMLImageDataWriter> writer;
//   writer->SetFileName(file.c_str());
//   writer->SetInputData(mask->GetOutput());
//   writer->Write();
//   }

//   {
//     transformFilter->Update();
//     std::string file = "D:\\Workspace\\Data\\test/Scalpelmesh_";
//   file += std::to_string(count);
//   file += ".vtp";
//   vtkNew<vtkXMLPolyDataWriter> writer;
//   writer->SetFileName(file.c_str());
//   writer->SetInputData(transformFilter->GetOutput());
//   writer->Write();
// }

// {
//   normals->Update();
//   std::string file = "D:\\Workspace\\Data\\test/Scalpelmesh_normal_";
// file += std::to_string(count);
// file += ".vtp";
// vtkNew<vtkXMLPolyDataWriter> writer;
// writer->SetFileName(file.c_str());
// writer->SetInputData(normals->GetOutput());
// writer->Write();
// }

// {
//   normals->Update();
//   std::string file = "D:\\Workspace\\Data\\test/FP_";
// file += std::to_string(count);
// file += ".vtp";
// vtkNew<vtkSphereSource> sphereSource;
// sphereSource->SetCenter(cameraFP[0], cameraFP[1], cameraFP[2]);
// sphereSource->SetRadius(100.0);
// sphereSource->Update();
// vtkNew<vtkXMLPolyDataWriter> writer;
// writer->SetFileName(file.c_str());
// writer->SetInputData(sphereSource->GetOutput());
// writer->Write();
// }

// {
//   normals->Update();
//   std::string file = "D:\\Workspace\\Data\\test/cameraPos_";
// file += std::to_string(count);
// file += ".vtp";
// vtkNew<vtkSphereSource> sphereSource;
// sphereSource->SetCenter(cameraPos[0], cameraPos[1], cameraPos[2]);
// sphereSource->SetRadius(100.0);
// sphereSource->Update();
// vtkNew<vtkXMLPolyDataWriter> writer;
// writer->SetFileName(file.c_str());
// writer->SetInputData(sphereSource->GetOutput());
// writer->Write();
// }

  count++;

}