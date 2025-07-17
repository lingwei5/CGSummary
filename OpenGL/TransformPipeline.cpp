// This example describes the transformation pipeline. Robotic arm was used to
// demonstrate an example.
#include <vtkActor.h>
#include <vtkCylinderSource.h>
#include <vtkNamedColors.h>
#include <vtkNew.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkTransform.h>
#include <vtkXMLPolyDataWriter.h>
#include <vtkXMLPolyDataReader.h>

int main(int, char*[])
{
  vtkNew<vtkNamedColors> colors;

  vtkNew<vtkRenderer> ren1;
  ren1->SetBackground(colors->GetColor3d("MidnightBlue").GetData());

  vtkNew<vtkRenderWindow> renWin;
  renWin->AddRenderer(ren1);
  renWin->SetSize(600, 600);
  renWin->SetWindowName("Robotic Arm");

  vtkNew<vtkRenderWindowInteractor> iren;
  iren->SetRenderWindow(renWin);

  vtkNew<vtkXMLPolyDataReader> reader;
  reader->SetFileName("D:\\Study\\OpenSource\\vtk-examples\\src\\Testing\\Data\\cow.vtp"); // robotic_arm.vtp
  reader->Update();

  // arm
  // vtkNew<vtkCylinderSource> arm;
  // arm->SetRadius(8);
  // arm->SetHeight(20);
  // arm->SetResolution(20);
  // arm->SetCenter(10, 0, 0);

  // {
  //   vtkNew<vtkXMLPolyDataWriter> writer;
  //   writer->SetFileName("D:\\Workspace\\Data\\test\\arm.vtp"); // arm.vtp
  //   writer->SetInputConnection(arm->GetOutputPort());
  //   writer->Write();
  // }

  auto arm = reader->GetOutput();

  vtkNew<vtkPolyDataMapper> armMapper;
  armMapper->SetInputConnection(reader->GetOutputPort());


  vtkNew<vtkActor> armActor;
  // armActor->SetUserTransform(armTransform);
  armActor->SetMapper(armMapper);
  armActor->GetProperty()->SetColor(colors->GetColor3d("SandyBrown").GetData());

  vtkNew<vtkXMLPolyDataReader> reader2;
  reader2->SetFileName("D:\\trans_cow.vtp"); // robotic_arm.vtp
  reader2->Update();
  // vtkNew<vtkPolyData> arm2;
  // arm2->DeepCopy(arm);
  vtkNew<vtkPolyDataMapper> armMapper2;
  armMapper2->SetInputConnection(reader2->GetOutputPort());
  // armMapper2->SetInputData(arm2);

  vtkNew<vtkTransform> armTransform;
  // armTransform->PostMultiply();
  //默认是PreMultiply，如果是PostMultiply，则变换的顺序是要倒过来
  //1. 先将坐标系平移到(10,0,0)，基本上是第二个牛的原点
  armTransform->Translate(10, 0, 0);
  // armTransform->GetMatrix()->Print(std::cout);
  auto mat = armTransform->GetMatrix();
  // armTransform->RotateY(180);
  //2. 在当前坐标系下旋转90度
  armTransform->RotateY(90);//
  mat = armTransform->GetMatrix();

  //3. 平移回变换前的坐标系进行显示
  armTransform->Translate(-10, 0, 0);
  mat = armTransform->GetMatrix();

  vtkNew<vtkActor> armActor2;
  // armActor2->SetOrigin(10, 0, 0);
  armActor2->SetUserTransform(armTransform);

  armActor2->SetMapper(armMapper2);
  armActor2->GetProperty()->SetColor(colors->GetColor3d("RoyalBLue").GetData());

ren1->AddActor(armActor);
ren1->AddActor(armActor2);


// // armTransform->PostMultiply();
//   armTransform->Translate(10, 0, 0);
//   // armTransform->GetMatrix()->Print(std::cout);
//   auto mat = armTransform->GetMatrix();
//   renWin->Render();
//   // armTransform->RotateY(180);
//   armTransform->RotateY(90);
//   mat = armTransform->GetMatrix();
//   renWin->Render();
//   armTransform->Translate(-10, 0, 0);
//   mat = armTransform->GetMatrix();
//   renWin->Render();

// for(int i=0;i<50;i++)
// {
//   // armTransform->Identity();//每次都在固定坐标系开始变换，注掉后则是每次在上一次的坐标系下变换
//   // armTransform->Translate(i, 0, 0);
//   armTransform->RotateY(10);
//   renWin->Render();
// }

  // // forearm
  // vtkNew<vtkCylinderSource> forearm;
  // forearm->SetRadius(6);
  // forearm->SetHeight(15);
  // forearm->SetResolution(20);
  // forearm->SetCenter(*(arm->GetCenter()),
  //                    *(arm->GetCenter() + 1) + forearm->GetHeight(),
  //                    *(arm->GetCenter() + 2));

                    //  {
                    //   vtkNew<vtkXMLPolyDataWriter> writer;
                    //   writer->SetFileName("D:\\Workspace\\Data\\test\\forearm.vtp"); // forearm.vtp
                    //   writer->SetInputConnection(forearm->GetOutputPort());
                    //   writer->Write();
                    // }

  // vtkNew<vtkPolyDataMapper> forearmMapper;
  // forearmMapper->SetInputConnection(forearm->GetOutputPort());

  // vtkNew<vtkTransform> forearmTransform;
  // forearmTransform->SetInput(armTransform);

  // vtkNew<vtkActor> forearmActor;
  // forearmActor->SetUserTransform(forearmTransform);
  // forearmActor->SetMapper(forearmMapper);
  // forearmActor->GetProperty()->SetColor(
  //     colors->GetColor3d("RoyalBLue").GetData());

  // // hand
  // vtkNew<vtkCylinderSource> hand;
  // hand->SetRadius(4);
  // hand->SetHeight(10);
  // hand->SetResolution(20);
  // hand->SetCenter(*(forearm->GetCenter()),
  //                 *(forearm->GetCenter() + 1) + hand->GetHeight(),
  //                 *(forearm->GetCenter() + 2));

  //                 // {
  //                 //   vtkNew<vtkXMLPolyDataWriter> writer;
  //                 //   writer->SetFileName("D:\\Workspace\\Data\\test\\hand.vtp"); // hand.vtp
  //                 //   writer->SetInputConnection(hand->GetOutputPort());
  //                 //   writer->Write();
  //                 // }
  // vtkNew<vtkPolyDataMapper> handMapper;
  // handMapper->SetInputConnection(hand->GetOutputPort());

  // vtkNew<vtkTransform> handTransform;
  // handTransform->SetInput(forearmTransform);

  // vtkNew<vtkActor> handActor;
  // handActor->SetUserTransform(handTransform);
  // handActor->SetMapper(handMapper);
  // handActor->GetProperty()->SetColor(
  //     colors->GetColor3d("GreenYellow").GetData());

  // ren1->AddActor(armActor);
  // ren1->AddActor(forearmActor);
  // ren1->AddActor(handActor);

  // renWin->Render();

  // // execution of robot arm motion
  // for (int i = 0; i < 45; i++)
  // {
  //   armTransform->Identity();
  //   armTransform->RotateZ(-i);
  //   renWin->Render();
  // }
  // // execution of robot forearm motion
  // auto center = forearm->GetCenter();
  // for (int i = 44; i < 45; i++)
  // {
  //   forearmTransform->Identity();
  //   forearmTransform->PostMultiply();
  //   forearmTransform->Translate(center);
  //   forearmTransform->RotateY(90);
  //   forearmTransform->Translate(-center[0], -center[1], -center[2]);
  //   renWin->Render();
  // }

  iren->Initialize();
  iren->Start();

  return EXIT_SUCCESS;
}
