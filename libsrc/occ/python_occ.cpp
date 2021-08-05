#ifdef NG_PYTHON
#ifdef OCCGEOMETRY

#include <../general/ngpython.hpp>
#include <core/python_ngcore.hpp>
#include "../meshing/python_mesh.hpp"

#include <meshing.hpp>
#include <occgeom.hpp>

#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax2d.hxx>
#include <gp_Trsf.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
// #include <XCAFDoc_VisMaterialTool.hxx>
#include <TDF_Attribute.hxx>
#include <Standard_GUID.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GC_MakeSegment.hxx>
#include <GC_MakeCircle.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>

#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepLib.hxx>

#include <Geom2d_Curve.hxx>
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <GCE2d_MakeCircle.hxx>

#if OCC_VERSION_MAJOR>=7 && OCC_VERSION_MINOR>=4
#define OCC_HAVE_DUMP_JSON
#endif

using namespace netgen;

namespace netgen
{
  extern std::shared_ptr<NetgenGeometry> ng_geometry;
}

static string occparameter_description = R"delimiter(
OCC Specific Meshing Parameters
-------------------------------

closeedgefac: Optional[float] = 2.
  Factor for meshing close edges, if None it is disabled.

minedgelen: Optional[float] = 0.001
  Minimum edge length to be used for dividing edges to mesh points. If
  None this is disabled.

)delimiter";

void CreateOCCParametersFromKwargs(OCCParameters& occparam, py::dict kwargs)
{
  if(kwargs.contains("minedgelen"))
    {
      auto val = kwargs.attr("pop")("minedgelen");
      if(val.is_none())
        occparam.resthminedgelenenable = false;
      else
        {
          occparam.resthminedgelen = py::cast<double>(val);
          occparam.resthminedgelenenable = true;
        }
    }
}


DLL_HEADER void ExportNgOCC(py::module &m) 
{
  m.attr("occ_version") = OCC_VERSION_COMPLETE;
  // not working, since occ - exceptions don't derive from std::exception
  // py::register_exception<Standard_Failure>(m, "OCC-Exception"); 
  
  py::class_<OCCGeometry, shared_ptr<OCCGeometry>, NetgenGeometry> (m, "OCCGeometry", R"raw_string(Use LoadOCCGeometry to load the geometry from a *.step file.)raw_string")
    .def(py::init<>())
    /*
    .def(py::init<const TopoDS_Shape&>(), py::arg("shape"),
         "Create Netgen OCCGeometry from existing TopoDS_Shape")
    */
    .def(py::init([] (const TopoDS_Shape& shape)
                  {
                    auto geo = make_shared<OCCGeometry> (shape);
                    ng_geometry = geo;
                    
                    // geo->BuildFMap();
                    // geo->CalcBoundingBox();
                    return geo;
                  }), py::arg("shape"),
         "Create Netgen OCCGeometry from existing TopoDS_Shape")
    
    .def(py::init([] (const std::vector<TopoDS_Shape> shapes)
                  {
                    BOPAlgo_Builder builder;
                    for (auto & s : shapes)
                      builder.AddArgument(s);                    
                    builder.Perform();
                    cout << "glued together" << endl;
                    
#ifdef OCC_HAVE_HISTORY
                    Handle(BRepTools_History) history = builder.History ();
                    
                    for (auto & s : shapes)
                      for (TopExp_Explorer e(s, TopAbs_SOLID); e.More(); e.Next())
                        if (auto name = OCCGeometry::global_shape_properties[e.Current().TShape()].name)
                          {
                            TopTools_ListOfShape modlist = history->Modified(e.Current());
                            for (auto mods : modlist)
                              OCCGeometry::global_shape_properties[mods.TShape()].name = *name;
                          }
#endif // OCC_HAVE_HISTORY

                    auto geo = make_shared<OCCGeometry> (builder.Shape());
                    ng_geometry = geo;
                    // geo->BuildFMap();
                    // geo->CalcBoundingBox();
                    return geo;
                  }), py::arg("shape"),
         "Create Netgen OCCGeometry from existing TopoDS_Shape")
    
    .def(py::init([] (const string& filename)
                  {
                    shared_ptr<OCCGeometry> geo;
                    if(EndsWith(filename, ".step") || EndsWith(filename, ".stp"))
                      geo.reset(LoadOCC_STEP(filename.c_str()));
                    else if(EndsWith(filename, ".brep"))
                      geo.reset(LoadOCC_BREP(filename.c_str()));
                    else if(EndsWith(filename, ".iges"))
                      geo.reset(LoadOCC_IGES(filename.c_str()));
                    else
                      throw Exception("Cannot load file " + filename + "\nValid formats are: step, stp, brep, iges");
                    ng_geometry = geo;
                    return geo;
                  }), py::arg("filename"),
        "Load OCC geometry from step, brep or iges file")
    .def(NGSPickle<OCCGeometry>())
    .def("Glue", &OCCGeometry::GlueGeometry)
    .def("Heal",[](OCCGeometry & self, double tolerance, bool fixsmalledges, bool fixspotstripfaces, bool sewfaces, bool makesolids, bool splitpartitions)
         {
           self.tolerance = tolerance;
           self.fixsmalledges = fixsmalledges;
           self.fixspotstripfaces = fixspotstripfaces;
           self.sewfaces = sewfaces;
           self.makesolids = makesolids;
           self.splitpartitions = splitpartitions;

           self.HealGeometry();
           self.BuildFMap();
         },py::arg("tolerance")=1e-3, py::arg("fixsmalledges")=true, py::arg("fixspotstripfaces")=true, py::arg("sewfaces")=true, py::arg("makesolids")=true, py::arg("splitpartitions")=false,R"raw_string(Heal the OCCGeometry.)raw_string",py::call_guard<py::gil_scoped_release>())
    .def("SetFaceMeshsize", [](OCCGeometry& self, size_t fnr, double meshsize)
                            {
                              self.SetFaceMaxH(fnr, meshsize);
                            }, "Set maximum meshsize for face fnr. Face numbers are 0 based.")
    .def("_visualizationData", [] (shared_ptr<OCCGeometry> occ_geo)
         {
           std::vector<float> vertices;
           std::vector<int> trigs;
           std::vector<float> normals;
           std::vector<float> min = {std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max()};
           std::vector<float> max = {std::numeric_limits<float>::lowest(),
                               std::numeric_limits<float>::lowest(),
                               std::numeric_limits<float>::lowest()};
           std::vector<string> surfnames;
           auto box = occ_geo->GetBoundingBox();
           for(int i = 0; i < 3; i++)
             {
               min[i] = box.PMin()[i];
               max[i] = box.PMax()[i];
             }
           occ_geo->BuildVisualizationMesh(0.01);
           gp_Pnt2d uv;
           gp_Pnt pnt;
           gp_Vec n;
           gp_Pnt p[3];
           int count = 0;
           for (int i = 1; i <= occ_geo->fmap.Extent(); i++)
             {
               surfnames.push_back("occ_surface" + to_string(i));
               auto face = TopoDS::Face(occ_geo->fmap(i));
               auto surf = BRep_Tool::Surface(face);
               TopLoc_Location loc;
               BRepAdaptor_Surface sf(face, Standard_False);
               BRepLProp_SLProps prop(sf, 1, 1e-5);
               Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation (face, loc);
               if (triangulation.IsNull())
                 cout << "cannot visualize face " << i << endl;
               trigs.reserve(trigs.size() + triangulation->NbTriangles()*4);
               vertices.reserve(vertices.size() + triangulation->NbTriangles()*3*3);
               normals.reserve(normals.size() + triangulation->NbTriangles()*3*3);
               for (int j = 1; j < triangulation->NbTriangles()+1; j++)
                 {
                   auto triangle = (triangulation->Triangles())(j);
                   for (int k = 1; k < 4; k++)
                     p[k-1] = (triangulation->Nodes())(triangle(k)).Transformed(loc);
                   for (int k = 1; k < 4; k++)
                     {
                       vertices.insert(vertices.end(),{float(p[k-1].X()), float(p[k-1].Y()), float(p[k-1].Z())});
                       trigs.insert(trigs.end(),{count, count+1, count+2,i});
                       count += 3;
                       uv = (triangulation->UVNodes())(triangle(k));
                       prop.SetParameters(uv.X(), uv.Y());
                       if (prop.IsNormalDefined())
                         n = prop.Normal();
                       else
                         {
                           gp_Vec a(p[0], p[1]);
                           gp_Vec b(p[0], p[2]);
                           n = b^a;
                         }
                       if (face.Orientation() == TopAbs_REVERSED) n*= -1;
                       normals.insert(normals.end(),{float(n.X()), float(n.Y()), float(n.Z())});
                     }
                 }
             }
            py::gil_scoped_acquire ac;
            py::dict res;
            py::list snames;
            for(auto name : surfnames)
              snames.append(py::cast(name));
            res["vertices"] = MoveToNumpy(vertices);
            res["triangles"] = MoveToNumpy(trigs);
            res["normals"] = MoveToNumpy(normals);
            res["surfnames"] = snames;
            res["min"] = MoveToNumpy(min);
            res["max"] = MoveToNumpy(max);
            return res;
         }, py::call_guard<py::gil_scoped_release>())
    .def("GenerateMesh", [](shared_ptr<OCCGeometry> geo,
                            MeshingParameters* pars, py::kwargs kwargs)
                         {
                           MeshingParameters mp;
                           OCCParameters occparam;
                           {
                             py::gil_scoped_acquire aq;
                             if(pars)
                               {
                                 auto mp_kwargs = CreateDictFromFlags(pars->geometrySpecificParameters);
                                 CreateOCCParametersFromKwargs(occparam, mp_kwargs);
                                 mp = *pars;
                               }
                             CreateOCCParametersFromKwargs(occparam, kwargs);
                             CreateMPfromKwargs(mp, kwargs);
                           }
                           geo->SetOCCParameters(occparam);
                           auto mesh = make_shared<Mesh>();
                           mesh->SetGeometry(geo);
                           auto result = geo->GenerateMesh(mesh, mp);
                           if(result != 0)
                             throw Exception("Meshing failed!");
                           SetGlobalMesh(mesh);
                           ng_geometry = geo;
                           return mesh;
                         }, py::arg("mp") = nullptr,
      py::call_guard<py::gil_scoped_release>(),
         (meshingparameter_description + occparameter_description).c_str())
    .def_property_readonly("shape", [](const OCCGeometry & self) { return self.GetShape(); })
    ;

  py::enum_<TopAbs_ShapeEnum>(m, "TopAbs_ShapeEnum", "Enumeration of all supported TopoDS_Shapes")
    .value("COMPOUND", TopAbs_COMPOUND)   .value("COMPSOLID", TopAbs_COMPSOLID)
    .value("SOLID", TopAbs_SOLID)       .value("SHELL", TopAbs_SHELL)
    .value("FACE", TopAbs_FACE)         .value("WIRE", TopAbs_WIRE)
    .value("EDGE", TopAbs_EDGE) .value("VERTEX", TopAbs_VERTEX)
    .value("SHAPE", TopAbs_SHAPE)
    .export_values()
    ;

  py::class_<gp_Pnt>(m, "gp_Pnt")
    .def(py::init([] (py::tuple pnt)
                  {
                    if (py::len(pnt) != 3)
                      throw Exception("need 3-tuple to create gp_Pnt");
                    
                    return gp_Pnt(py::cast<double>(pnt[0]),
                                  py::cast<double>(pnt[1]),
                                  py::cast<double>(pnt[2]));
                  }))
    .def(py::init([] (double x, double y, double z) {
          return gp_Pnt(x, y, z);
        }))
    .def_property("x", [](gp_Pnt&p) { return p.X(); }, [](gp_Pnt&p,double x) { p.SetX(x); })
    .def_property("y", [](gp_Pnt&p) { return p.Y(); }, [](gp_Pnt&p,double y) { p.SetY(y); })
    .def_property("z", [](gp_Pnt&p) { return p.Z(); }, [](gp_Pnt&p,double z) { p.SetZ(z); })
    .def("__str__", [] (const gp_Pnt & p) {
        stringstream str;
        str << "(" << p.X() << ", " << p.Y() << ", " << p.Z() << ")";
        return str.str();
      })
    // .def(py::self - py::self)
    .def("__sub__", [](gp_Pnt p1, gp_Pnt p2) { return gp_Vec(p1.X()-p2.X(), p1.Y()-p2.Y(), p1.Z()-p2.Z()); })
    .def("__add__", [](gp_Pnt p, gp_Vec v) { return gp_Pnt(p.X()+v.X(), p.Y()+v.Y(), p.Z()+v.Z()); })
    ;
  
  py::class_<gp_Vec>(m, "gp_Vec")
    .def(py::init([] (py::tuple vec)
                  {
                    return gp_Vec(py::cast<double>(vec[0]),
                                  py::cast<double>(vec[1]),
                                  py::cast<double>(vec[2]));
                  }))
    .def(py::init([] (double x, double y, double z) {
          return gp_Vec(x, y, z);
        }))
    .def_property("x", [](gp_Pnt&p) { return p.X(); }, [](gp_Pnt&p,double x) { p.SetX(x); })
    .def_property("y", [](gp_Pnt&p) { return p.Y(); }, [](gp_Pnt&p,double y) { p.SetY(y); })
    .def_property("z", [](gp_Pnt&p) { return p.Z(); }, [](gp_Pnt&p,double z) { p.SetZ(z); })
    .def("__str__", [] (const gp_Pnt & p) {
        stringstream str;
        str << "(" << p.X() << ", " << p.Y() << ", " << p.Z() << ")";
        return str.str();
      })
    .def("__add__", [](gp_Vec v1, gp_Vec v2) { return gp_Vec(v1.X()+v2.X(), v1.Y()+v2.Y(), v1.Z()+v2.Z()); })
    .def("__rmul__", [](gp_Vec v, double s) { return gp_Vec(s*v.X(), s*v.Y(), s*v.Z()); })
    .def("__neg__", [](gp_Vec v) { return gp_Vec(-v.X(), -v.Y(), -v.Z()); })    
    ;

  py::class_<gp_Dir>(m, "gp_Dir")
    .def(py::init([] (py::tuple dir)
                  {
                    return gp_Dir(py::cast<double>(dir[0]),
                                  py::cast<double>(dir[1]),
                                  py::cast<double>(dir[2]));
                  }))
    .def(py::init([] (double x, double y, double z) {
          return gp_Dir(x, y, z);
        }))
    .def(py::init<gp_Vec>())
    .def("__str__", [] (const gp_Pnt & p) {
        stringstream str;
        str << "(" << p.X() << ", " << p.Y() << ", " << p.Z() << ")";
        return str.str();
      })
    ;
  
  py::class_<gp_Ax1>(m, "gp_Ax1")
    .def(py::init([](gp_Pnt p, gp_Dir d) {
          return gp_Ax1(p,d);
        }))
    ;
  py::class_<gp_Ax2>(m, "gp_Ax2")
    .def(py::init([](gp_Pnt p, gp_Dir d) {
          return gp_Ax2(p,d);
        }))
    .def(py::init([](const gp_Ax3 & ax3) {
          return gp_Ax2(ax3.Ax2());
        }))
    ;

  py::class_<gp_Ax3>(m, "gp_Ax3")
    .def(py::init([](gp_Pnt p, gp_Dir N, gp_Dir Vx) {
          return gp_Ax3(p,N, Vx);
        }), py::arg("p"), py::arg("n"), py::arg("x"))
    .def(py::init<gp_Ax2>())
    .def_property("p", [](gp_Ax3 & ax) { return ax.Location(); }, [](gp_Ax3&ax, gp_Pnt p) { ax.SetLocation(p); })
    ;


  py::class_<gp_Pnt2d>(m, "gp_Pnt2d")
    .def(py::init([] (py::tuple pnt)
                  {
                    if (py::len(pnt) != 2)
                      throw Exception("need 2-tuple to create gp_Pnt2d");
                    return gp_Pnt2d(py::cast<double>(pnt[0]),
                                    py::cast<double>(pnt[1]));
                  }))
    .def(py::init([] (double x, double y) {
          return gp_Pnt2d(x, y);
        }))
    ;
  py::class_<gp_Vec2d>(m, "gp_Vec2d")
    .def(py::init([] (py::tuple vec)
                  {
                    if (py::len(vec) != 2)
                      throw Exception("need 2-tuple to create gp_Vec2d");                    
                    return gp_Vec2d(py::cast<double>(vec[0]),
                                    py::cast<double>(vec[1]));
                  }))
    .def(py::init([] (double x, double y) {
          return gp_Vec2d(x, y);
        }))
    ;

  py::class_<gp_Dir2d>(m, "gp_Dir2d")
    .def(py::init([] (py::tuple dir)
                  {
                    if (py::len(dir) != 2)
                      throw Exception("need 2-tuple to create gp_Dir2d");                    
                    return gp_Dir2d(py::cast<double>(dir[0]),
                                    py::cast<double>(dir[1]));
                  }))
    .def(py::init([] (double x, double y) {
          return gp_Dir2d(x, y);
        }))
    ;

  py::class_<gp_Ax2d>(m, "gp_Ax2d")
    .def(py::init([](gp_Pnt2d p, gp_Dir2d d) {
          return gp_Ax2d(p,d);
        }))
    ;


  
  py::class_<gp_Trsf>(m, "gp_Trsf")
    .def(py::init<>())    
    .def("SetMirror", [] (gp_Trsf & trafo, const gp_Ax1 & ax) { trafo.SetMirror(ax); return trafo; })
    .def_static("Translation", [] (const gp_Vec & v) { gp_Trsf trafo; trafo.SetTranslation(v); return trafo; })
    .def_static("Scale", [] (const gp_Pnt & p, double s) { gp_Trsf trafo; trafo.SetScale(p,s); return trafo; })    
    .def_static("Mirror", [] (const gp_Ax1 & ax) { gp_Trsf trafo; trafo.SetMirror(ax); return trafo; })
    .def_static("Rotation", [] (const gp_Ax1 & ax, double ang) { gp_Trsf trafo; trafo.SetRotation(ax, ang); return trafo; })
    .def_static("Rotation", [] (const gp_Pnt & p, const gp_Dir & d, double ang)
                { gp_Trsf trafo; trafo.SetRotation(gp_Ax1(p,d), ang); return trafo; })    
    .def_static("Transformation", [] (const gp_Ax3 & ax) { gp_Trsf trafo; trafo.SetTransformation(ax); return trafo; })
    .def_static("Transformation", [] (const gp_Ax3 & from, const gp_Ax3 to)
                { gp_Trsf trafo; trafo.SetTransformation(from, to); return trafo; })
    .def(py::self * py::self)
    .def("__call__", [] (gp_Trsf & trafo, const TopoDS_Shape & shape) {
        return BRepBuilderAPI_Transform(shape, trafo).Shape();
      })
    .def("__str__", [](gp_Trsf & trafo)
    {
      stringstream str;
      gp_XYZ xyz = trafo.TranslationPart();
      str << xyz.X() << ", " << xyz.Y() << ", " << xyz.Z();
      return str.str();
    })
    ;

  py::class_<TopLoc_Location>(m, "TopLoc_Location")
    .def(py::init<gp_Trsf>())
    .def("Transformation", [](const TopLoc_Location & loc) { return loc.Transformation(); })
    ;

  py::implicitly_convertible<py::tuple, gp_Pnt>();
  py::implicitly_convertible<py::tuple, gp_Vec>();
  py::implicitly_convertible<py::tuple, gp_Dir>();
  py::implicitly_convertible<gp_Vec, gp_Dir>();  
  py::implicitly_convertible<py::tuple, gp_Pnt2d>();  
  py::implicitly_convertible<py::tuple, gp_Vec2d>();  
  py::implicitly_convertible<py::tuple, gp_Dir2d>();


  py::implicitly_convertible<gp_Ax3, gp_Ax2>();

  static gp_Vec ex(1,0,0), ey(0,1,0), ez(0,0,1);
  m.attr("X") = py::cast(&ex);
  m.attr("Y") = py::cast(&ey);
  m.attr("Z") = py::cast(&ez);

  class ListOfShapes : public std::vector<TopoDS_Shape> { };
  
  
  py::class_<TopoDS_Shape> (m, "TopoDS_Shape")
    .def("__str__", [] (const TopoDS_Shape & shape)
         {
           stringstream str;
#ifdef OCC_HAVE_DUMP_JSON
           shape.DumpJson(str);
#endif // OCC_HAVE_DUMP_JSON
           return str.str();
         })
    
    .def("ShapeType", [] (const TopoDS_Shape & shape)
         { return shape.ShapeType(); })
    .def_property_readonly("type", [](const TopoDS_Shape & shape)
                           { return shape.ShapeType(); })    
    
    .def("SubShapes", [] (const TopoDS_Shape & shape, TopAbs_ShapeEnum & type)
         {
           /*
           py::list sub;
           TopExp_Explorer e;
           for (e.Init(shape, type); e.More(); e.Next())
             {
               switch (type)
                 {
                 case TopAbs_FACE:
                   sub.append(TopoDS::Face(e.Current())); break;
                 default:
                   sub.append(e.Current());
                 }
             }
           return sub;
           */
           ListOfShapes sub;
           for (TopExp_Explorer e(shape, type); e.More(); e.Next())
             sub.push_back(e.Current());
           return sub;
         })
    
    .def_property_readonly("faces", [] (const TopoDS_Shape & shape)
         {
           ListOfShapes sub;
           for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next())
             sub.push_back(e.Current());
           return sub;
         })
    .def_property_readonly("edges", [] (const TopoDS_Shape & shape)
         {
           ListOfShapes sub;
           for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next())
             sub.push_back(e.Current());
           return sub;
         })
    .def_property_readonly("vertices", [] (const TopoDS_Shape & shape)
         {
           ListOfShapes sub;
           for (TopExp_Explorer e(shape, TopAbs_VERTEX); e.More(); e.Next())
             sub.push_back(e.Current());
           return sub;
         })

    .def("Properties", [] (const TopoDS_Shape & shape)
         {
           GProp_GProps props;
           switch (shape.ShapeType())
             {
             case TopAbs_FACE:
               BRepGProp::SurfaceProperties (shape, props); break;
             default:
               BRepGProp::LinearProperties(shape, props);
               // throw Exception("Properties implemented only for FACE");
             }
           double mass = props.Mass();
           gp_Pnt center = props.CentreOfMass();
           return tuple( py::cast(mass), py::cast(center) );
         })
    .def_property_readonly("center", [](const TopoDS_Shape & shape) {
           GProp_GProps props;
           switch (shape.ShapeType())
             {
             case TopAbs_FACE:
               BRepGProp::SurfaceProperties (shape, props); break;
             default:
               BRepGProp::LinearProperties(shape, props);
             }
           return props.CentreOfMass();
      })
    
    .def("bc", [](const TopoDS_Shape & shape, const string & name)
         {
           for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next())
             OCCGeometry::global_shape_properties[e.Current().TShape()].name = name;
           return shape;
         })

    .def("mat", [](const TopoDS_Shape & shape, const string & name)
         {
           for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next())
             OCCGeometry::global_shape_properties[e.Current().TShape()].name = name;
           return shape;
         })
    
    .def_property("name", [](const TopoDS_Shape & self) {
        if (auto name = OCCGeometry::global_shape_properties[self.TShape()].name)
          return *name;
        else
          return string();
      }, [](const TopoDS_Shape & self, string name) {
        OCCGeometry::global_shape_properties[self.TShape()].name = name;            
      })

    .def_property("col", [](const TopoDS_Shape & self) {
        auto it = OCCGeometry::global_shape_properties.find(self.TShape());
        Vec<3> col(0.2, 0.2, 0.2);
        if (it != OCCGeometry::global_shape_properties.end() && it->second.col)
          col = *it->second.col; // .value();
        return std::vector<double> ( { col(0), col(1), col(2) } );
      }, [](const TopoDS_Shape & self, std::vector<double> c) {
        Vec<3> col(c[0], c[1], c[2]);
        OCCGeometry::global_shape_properties[self.TShape()].col = col;    
      })
    
    .def_property("location",
                  [](const TopoDS_Shape & shape) { return shape.Location(); },
                  [](TopoDS_Shape & shape, const TopLoc_Location & loc)
                  { shape.Location(loc); })
    .def("Located", [](const TopoDS_Shape & shape, const TopLoc_Location & loc)
                  { return shape.Located(loc); })

    .def("__add__", [] (const TopoDS_Shape & shape1, const TopoDS_Shape & shape2) {
        return BRepAlgoAPI_Fuse(shape1, shape2).Shape();
      })
    
    .def("__mul__", [] (const TopoDS_Shape & shape1, const TopoDS_Shape & shape2) {
        // return BRepAlgoAPI_Common(shape1, shape2).Shape();
        
        BRepAlgoAPI_Common builder(shape1, shape2);
#ifdef OCC_HAVE_HISTORY
        Handle(BRepTools_History) history = builder.History ();

        /*
          // work in progress ...
        TopTools_ListOfShape modlist = history->Modified(shape1);
        for (auto s : modlist)
          cout << "modified from list el: " << s.ShapeType() << endl;
        */

        for (auto & s : { shape1, shape2 })
          for (TopExp_Explorer e(s, TopAbs_FACE); e.More(); e.Next())
            {
              auto & prop = OCCGeometry::global_shape_properties[e.Current().TShape()];
              for (auto smod : history->Modified(e.Current()))            
                OCCGeometry::global_shape_properties[smod.TShape()].Merge(prop);
            }        
#endif // OCC_HAVE_HISTORY
        
        return builder.Shape();
      })
    
    .def("__sub__", [] (const TopoDS_Shape & shape1, const TopoDS_Shape & shape2) {
        // return BRepAlgoAPI_Cut(shape1, shape2).Shape();
        
        BRepAlgoAPI_Cut builder(shape1, shape2);
#ifdef OCC_HAVE_HISTORY        
        Handle(BRepTools_History) history = builder.History ();

        for (auto s : { shape1, shape2 })
          for (TopExp_Explorer e(s, TopAbs_FACE); e.More(); e.Next())
            {
              /*
              const string & name = OCCGeometry::global_shape_names[e.Current().TShape()];
              for (auto s : history->Modified(e.Current()))            
                OCCGeometry::global_shape_names[s.TShape()] = name;
              */
              /*
              auto it = OCCGeometry::global_shape_cols.find(e.Current().TShape());
              if (it != OCCGeometry::global_shape_cols.end())
                for (auto s : history->Modified(e.Current()))
                  OCCGeometry::global_shape_cols[s.TShape()] = it->second;
              */
              auto propit = OCCGeometry::global_shape_properties.find(e.Current().TShape());
              if (propit != OCCGeometry::global_shape_properties.end())
                for (auto s : history->Modified(e.Current()))
                  OCCGeometry::global_shape_properties[s.TShape()].Merge(propit->second);
            }

        /*
        for (TopExp_Explorer e(shape2, TopAbs_FACE); e.More(); e.Next())
          {
            auto it = OCCGeometry::global_shape_cols[e.Current().TShape()];
            if (it != OCCGeometry::global_shape_cols.end())
              for (auto s : history->Modified(e.Current()))
                OCCGeometry::global_shape_cols[s.TShape()] = it->second;
          }        
        */
#endif // OCC_HAVE_HISTORY

        
        return builder.Shape();        
      })

    .def("Find", [](const TopoDS_Shape & shape, gp_Pnt p)
         {
           // find sub-shape contianing point
           // BRepClass_FaceClassifier::Perform  (p);
         })
    
    .def("MakeFillet", [](const TopoDS_Shape & shape, std::vector<TopoDS_Shape> edges, double r) {
        BRepFilletAPI_MakeFillet mkFillet(shape);
        for (auto e : edges)
          mkFillet.Add (r, TopoDS::Edge(e));
        return mkFillet.Shape();
      })
  
    .def("MakeThickSolid", [](const TopoDS_Shape & body, std::vector<TopoDS_Shape> facestoremove,
                              double offset, double tol) {
           TopTools_ListOfShape faces;
           for (auto f : facestoremove)
             faces.Append(f);
           
           BRepOffsetAPI_MakeThickSolid maker;
           maker.MakeThickSolidByJoin(body, faces, offset, tol);
           return maker.Shape();
         })
    
    .def("MakeTriangulation", [](const TopoDS_Shape & shape)
         {
           BRepTools::Clean (shape);
           double deflection = 0.01;
           BRepMesh_IncrementalMesh (shape, deflection, true);
         })
    
    .def("Triangulation", [](const TopoDS_Shape & shape)
         {
           // extracted from vsocc.cpp
           TopoDS_Face face;
           try
             {
               face = TopoDS::Face(shape);
             }
           catch (Standard_Failure & e)
             {
               e.Print (cout);
               throw NgException ("Triangulation: shape is not a face");
             }

           /*
           BRepTools::Clean (shape);
           double deflection = 0.01;
           BRepMesh_IncrementalMesh (shape, deflection, true);
           */

           Handle(Geom_Surface) surf = BRep_Tool::Surface (face);

           TopLoc_Location loc;
           Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation (face, loc);
           
           if (triangulation.IsNull())
             {
               BRepTools::Clean (shape);
               double deflection = 0.01;
               BRepMesh_IncrementalMesh (shape, deflection, true);
               triangulation = BRep_Tool::Triangulation (face, loc);               
             }
           // throw Exception("Don't have a triangulation, call 'MakeTriangulation' first");

           int ntriangles = triangulation -> NbTriangles();
           Array< std::array<Point<3>,3> > triangles;
           for (int j = 1; j <= ntriangles; j++)
             {
               Poly_Triangle triangle = (triangulation -> Triangles())(j);
               std::array<Point<3>,3> pts;
               for (int k = 0; k < 3; k++)
                 pts[k] = occ2ng( (triangulation -> Nodes())(triangle(k+1)).Transformed(loc) );
               
               triangles.Append ( pts );
             }
           
           // return MoveToNumpyArray(triangles);
           return triangles;
         })
    ;
  
  py::class_<TopoDS_Edge, TopoDS_Shape> (m, "TopoDS_Edge");
  py::class_<TopoDS_Wire, TopoDS_Shape> (m, "TopoDS_Wire");
  py::class_<TopoDS_Face, TopoDS_Shape> (m, "TopoDS_Face")
    .def(py::init([] (const TopoDS_Shape & shape) {
          return TopoDS::Face(shape);
        }))
    /*
    .def("surf", [] (TopoDS_Face face) -> Handle(Geom_Surface)
         {
           Handle(Geom_Surface) surf = BRep_Tool::Surface (face);
           return surf;
         })
    */
    ;
  py::class_<TopoDS_Solid, TopoDS_Shape> (m, "TopoDS_Solid");

  py::implicitly_convertible<TopoDS_Shape, TopoDS_Face>();

  
  py::class_<ListOfShapes> (m, "ListOfShapes")
    .def("__iter__", [](const ListOfShapes &s) { return py::make_iterator(s.begin(), s.end()); },
         py::keep_alive<0, 1>() /* Essential: keep object alive while iterator exists */)
    .def("__getitem__", [](const ListOfShapes & list, size_t i) {
        return list[i]; })
    .def("__add__", [](const ListOfShapes & l1, const ListOfShapes & l2) {
        ListOfShapes l = l1;
        for (auto s : l2) l.push_back(s);
        return l;
      } )
    .def("__add__", [](const ListOfShapes & l1, py::list l2) {
        ListOfShapes l = l1;
        for (auto s : l2) l.push_back(py::cast<TopoDS_Shape>(s));
        return l;
      } )
    .def("__len__", [](const ListOfShapes & l) { return l.size(); })
    .def("Max", [] (ListOfShapes & shapes, gp_Vec dir)
         {
           double maxval = -1e99;
           TopoDS_Shape maxshape;
           for (auto shape : shapes)
             {
               GProp_GProps props;
               switch (shape.ShapeType())
                 {
                 case TopAbs_FACE:
                   BRepGProp::SurfaceProperties (shape, props); break;
                 default:
                   BRepGProp::LinearProperties(shape, props);
                 }
               gp_Pnt center = props.CentreOfMass();
               
               double val = center.X()*dir.X() + center.Y()*dir.Y() + center.Z() * dir.Z();
               if (val > maxval)
                 {
                   maxval = val;
                   maxshape = shape;
                 }
             }
           return maxshape;
         })
    
    ;
         
  
  py::class_<Handle(Geom2d_Curve)> (m, "Geom2d_Curve")
    .def("Trim", [](Handle(Geom2d_Curve) curve, double u1, double u2) -> Handle(Geom2d_Curve)
         {
           return new Geom2d_TrimmedCurve (curve, u1, u2);
         })
    .def("Value", [](Handle(Geom2d_Curve) curve, double s) {
        return curve->Value(s);
      })
    .def_property_readonly("start", [](Handle(Geom2d_Curve) curve) {
        return curve->Value(curve->FirstParameter());
      })
    .def_property_readonly("end", [](Handle(Geom2d_Curve) curve) {
        return curve->Value(curve->LastParameter());
      })
    ;

  
  m.def("Sphere", [] (gp_Pnt cc, double r) {
      return BRepPrimAPI_MakeSphere (cc, r).Solid();
    });
  
  m.def("Cylinder", [] (gp_Pnt cpnt, gp_Dir cdir, double r, double h) {
      return BRepPrimAPI_MakeCylinder (gp_Ax2(cpnt, cdir), r, h).Solid();
    }, py::arg("p"), py::arg("d"), py::arg("r"), py::arg("h"));
  m.def("Cylinder", [] (gp_Ax2 ax, double r, double h) {
      return BRepPrimAPI_MakeCylinder (ax, r, h).Solid();
    }, py::arg("axis"), py::arg("r"), py::arg("h"));
  
  m.def("Box", [] (gp_Pnt cp1, gp_Pnt cp2) {
      return BRepPrimAPI_MakeBox (cp1, cp2).Solid();
    });

  m.def("Prism", [] (const TopoDS_Shape & face, gp_Vec vec) {
      return BRepPrimAPI_MakePrism (face, vec).Shape();
    });

  m.def("Pipe", [] (const TopoDS_Wire & spine, const TopoDS_Shape & profile) {
      return BRepOffsetAPI_MakePipe (spine, profile).Shape();
    }, py::arg("spine"), py::arg("profile"));

  // Handle(Geom2d_Ellipse) anEllipse1 = new Geom2d_Ellipse(anAx2d, aMajor, aMinor);
  m.def("Ellipse", [] (const gp_Ax2d & ax, double major, double minor) -> Handle(Geom2d_Curve)
        {
          return new Geom2d_Ellipse(ax, major, minor);
        });
  
  m.def("Segment", [](gp_Pnt2d p1, gp_Pnt2d p2) -> Handle(Geom2d_Curve) { 
      Handle(Geom2d_TrimmedCurve) curve = GCE2d_MakeSegment(p1, p2);
      return curve;
      // return BRepBuilderAPI_MakeEdge(curve).Edge();
      // return GCE2d_MakeSegment(p1, p2);      
    });
  
  m.def("Circle", [](gp_Pnt2d p1, double r) -> Handle(Geom2d_Curve) {
      Handle(Geom2d_Circle) curve = GCE2d_MakeCircle(p1, r);
      return curve;
      // gp_Ax2d ax; ax.SetLocation(p1);
      // return new Geom2d_Circle(ax, r);
    });
  
  m.def("Glue", [] (const std::vector<TopoDS_Shape> shapes) -> TopoDS_Shape
        {
          BOPAlgo_Builder builder;
          for (auto & s : shapes)
            {
              for (TopExp_Explorer e(s, TopAbs_SOLID); e.More(); e.Next())            
                builder.AddArgument(e.Current());
              if (s.ShapeType() == TopAbs_FACE)
                builder.AddArgument(s);
            }

          builder.Perform();

#ifdef OCC_HAVE_HISTORY          
          Handle(BRepTools_History) history = builder.History ();

          for (auto & s : shapes)
            for (TopExp_Explorer e(s, TopAbs_SOLID); e.More(); e.Next())
              {
                auto prop = OCCGeometry::global_shape_properties[e.Current().TShape()];
                for (auto mods : history->Modified(e.Current()))
                  OCCGeometry::global_shape_properties[mods.TShape()].Merge(prop);
              }
              /*
              {
                auto name = OCCGeometry::global_shape_names[e.Current().TShape()];
                for (auto mods : history->Modified(e.Current()))
                  OCCGeometry::global_shape_names[mods.TShape()] = name;
              }
              */
#endif // OCC_HAVE_HISTORY
          
          return builder.Shape();
        });

  m.def("Glue", [] (TopoDS_Shape shape) -> TopoDS_Shape
        {
          BOPAlgo_Builder builder;
          
          for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next())
            builder.AddArgument(e.Current());
          
          builder.Perform();
          
          if (builder.HasErrors())
            builder.DumpErrors(cout);
          if (builder.HasWarnings())
            builder.DumpWarnings(cout);

#ifdef OCC_HAVE_HISTORY
          Handle(BRepTools_History) history = builder.History ();

          for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next())
            {
              auto prop = OCCGeometry::global_shape_properties[e.Current().TShape()];
              for (auto mods : history->Modified(e.Current()))
                OCCGeometry::global_shape_properties[mods.TShape()].Merge(prop);
            }
#endif // OCC_HAVE_HISTORY
          
          return builder.Shape();
        });


  // py::class_<Handle(Geom_TrimmedCurve)> (m, "Geom_TrimmedCurve")
  // ;
  
  m.def("Segment", [](gp_Pnt p1, gp_Pnt p2) { 
      Handle(Geom_TrimmedCurve) curve = GC_MakeSegment(p1, p2);
      return BRepBuilderAPI_MakeEdge(curve).Edge();
    });
  m.def("Circle", [](gp_Pnt c, gp_Dir n, double r) {
	Handle(Geom_Circle) curve = GC_MakeCircle (c, n, r);
        return BRepBuilderAPI_MakeEdge(curve).Edge();
    });

  m.def("ArcOfCircle", [](gp_Pnt p1, gp_Pnt p2, gp_Pnt p3) { 
      Handle(Geom_TrimmedCurve) curve = GC_MakeArcOfCircle(p1, p2, p3);
      return BRepBuilderAPI_MakeEdge(curve).Edge();
    }, py::arg("p1"), py::arg("p2"), py::arg("p3"));
  
  m.def("ArcOfCircle", [](gp_Pnt p1, gp_Vec v, gp_Pnt p2) { 
      Handle(Geom_TrimmedCurve) curve = GC_MakeArcOfCircle(p1, v, p2);
      return BRepBuilderAPI_MakeEdge(curve).Edge();
    }, py::arg("p1"), py::arg("v"), py::arg("p2"));


  m.def("Edge", [](Handle(Geom2d_Curve) curve2d, TopoDS_Face face) {
      auto edge = BRepBuilderAPI_MakeEdge(curve2d, BRep_Tool::Surface (face)).Edge();
      BRepLib::BuildCurves3d(edge);
      return edge;
    });
  
  m.def("Wire", [](std::vector<TopoDS_Shape> edges) {
      BRepBuilderAPI_MakeWire builder;
      for (auto s : edges)
        switch (s.ShapeType())
          {
          case TopAbs_EDGE:
            try
              {
              builder.Add(TopoDS::Edge(s)); break;
              }
            catch (Standard_Failure & e)
              {
                e.Print(cout);
                throw NgException("cannot add to wire");
              }
          case TopAbs_WIRE:
            builder.Add(TopoDS::Wire(s)); break;
          default:
            throw Exception("can make wire only from edges and wires");
          }
      try
        {
          return builder.Wire();
        }
      catch (Standard_Failure & e)
        {
          e.Print(cout);
          throw NgException("error in wire builder");
        }
    });

  m.def("Face", [](TopoDS_Wire wire) {
      return BRepBuilderAPI_MakeFace(wire).Face();
    }, py::arg("w"));
  m.def("Face", [](const TopoDS_Face & face, const TopoDS_Wire & wire) {
      return BRepBuilderAPI_MakeFace(face, wire).Face();
    }, py::arg("f"), py::arg("w"));
  /*
     not yet working .... ?
  m.def("Face", [](const TopoDS_Face & face, std::vector<TopoDS_Wire> wires) {
      // return BRepBuilderAPI_MakeFace(face, wire).Face();
      cout << "build from list of wires" << endl;
      BRepBuilderAPI_MakeFace builder(face);
      for (auto w : wires)
        builder.Add(w);
      return builder.Face();
    }, py::arg("f"), py::arg("w"));
  m.def("Face", [](std::vector<TopoDS_Wire> wires) {
      cout << "face from wires" << endl;
      BRepBuilderAPI_MakeFace builder;
      for (auto w : wires)
        {
          cout << "add wire" << endl;
          builder.Add(w);
        }
      return builder.Face();
    }, py::arg("w"));
  */

  m.def("MakeFillet", [](TopoDS_Shape shape, std::vector<TopoDS_Shape> edges, double r) {
      throw Exception("call 'shape.MakeFilled'");
      BRepFilletAPI_MakeFillet mkFillet(shape);
      for (auto e : edges)
        mkFillet.Add (r, TopoDS::Edge(e));
      return mkFillet.Shape();
    });
  
  m.def("MakeThickSolid", [](TopoDS_Shape body, std::vector<TopoDS_Shape> facestoremove,
                             double offset, double tol) {
          throw Exception("call 'shape.MakeThickSolid'");
          TopTools_ListOfShape faces;
          for (auto f : facestoremove)
            faces.Append(f);
          
          BRepOffsetAPI_MakeThickSolid maker;
          maker.MakeThickSolidByJoin(body, faces, offset, tol);
          return maker.Shape();
        });

  m.def("ThruSections", [](std::vector<TopoDS_Shape> wires)
        {
          BRepOffsetAPI_ThruSections aTool(Standard_True);
          for (auto shape : wires)
            aTool.AddWire(TopoDS::Wire(shape));
          aTool.CheckCompatibility(Standard_False);
          return aTool.Shape();
        });
  
  m.def("LoadOCCGeometry",[] (const string & filename)
        {
          cout << "WARNING: LoadOCCGeometry is deprecated! Just use the OCCGeometry(filename) constructor. It is able to read brep and iges files as well!" << endl;
          ifstream ist(filename);
          OCCGeometry * instance = new OCCGeometry();
          instance = LoadOCC_STEP(filename.c_str());
          ng_geometry = shared_ptr<OCCGeometry>(instance, NOOP_Deleter);
          return ng_geometry;
        },py::call_guard<py::gil_scoped_release>());


  m.def("TestXCAF", [] (TopoDS_Shape shape) {

      /*static*/ Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
      cout << endl << endl << endl;
      cout << "app = " << *reinterpret_cast<void**>(&app) << endl;
      Handle(TDocStd_Document) doc;
      cout << "nbdocs = " << app->NbDocuments() << endl;
      if(app->NbDocuments() > 0)
      {
         app->GetDocument(1,doc);
         // app->Close(doc);
      }
      else
        app->NewDocument ("STEP-XCAF",doc);
      Handle(XCAFDoc_ShapeTool) shape_tool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
      Handle(XCAFDoc_MaterialTool) material_tool = XCAFDoc_DocumentTool::MaterialTool(doc->Main());
      // Handle(XCAFDoc_VisMaterialTool) vismaterial_tool = XCAFDoc_DocumentTool::VisMaterialTool(doc->Main());

      cout << "handle(shape) = " << *(void**)(void*)(&(shape.TShape())) << endl;
      
      TDF_LabelSequence doc_shapes;
      shape_tool->GetShapes(doc_shapes);
      cout << "shape tool nbentities: " << doc_shapes.Size() << endl;
      TDF_Label label = shape_tool -> FindShape(shape);
      cout << "shape label = " << endl << label << endl;
      if (label.IsNull()) return;
      cout << "nbattr = " << label.NbAttributes() << endl;
                                                     
                                                     
      if (!label.IsNull())
        {
          Handle(TDF_Attribute) attribute;
          cout << "create guid" << endl;
          // Standard_GUID guid("c4ef4200-568f-11d1-8940-080009dc3333");
          Standard_GUID guid("2a96b608-ec8b-11d0-bee7-080009dc3333");      
          cout << "have guid" << endl;
          cout << "find attrib " << label.FindAttribute(guid, attribute) << endl;
          cout << "attrib = " << attribute << endl;
          cout << "tag = " << label.Tag() << endl;
          cout << "father.tag = " << label.Father().Tag() << endl;
          cout << "Data = " << label.Data() << endl;
          
          cout << "nbchild = " << label.NbChildren() << endl;
          for (auto i : Range(label.NbChildren()))
            {
              TDF_Label child = label.FindChild(i+1);
              cout << "child[" << i << "] = " << child << endl;
              cout << "find attrib " << child.FindAttribute(guid, attribute) << endl;
              cout << "attrib = " << attribute << endl;
            }
          
          // cout << "findshape = " << shape_tool -> FindShape(shape) << endl;
          cout << "IsMaterial = " << material_tool->IsMaterial(label) << endl;
          // cout << "IsVisMaterial = " << vismaterial_tool->IsMaterial(label) << endl;
        }
    }, py::arg("shape")=TopoDS_Shape());
        
}

PYBIND11_MODULE(libNgOCC, m) {
  ExportNgOCC(m);
}

#endif // OCCGEOMETRY
#endif // NG_PYTHON
