/*+**************************************************************************/
/***                                                                      ***/
/***   This file is distributed under a BSD license.                      ***/
/***   See LICENSE.txt for details.                                       ***/
/***                                                                      ***/
/**************************************************************************+*/

#include "base/types.hpp"
#include "wz4lib/doc.hpp"
#include "wz4frlib/packfile.hpp"
#include "wz4frlib/packfilegen.hpp"
#include "wz4lib/version.hpp"
#include "util/painter.hpp"
#include "util/taskscheduler.hpp"
#include "extra/blobheap.hpp"
#include "extra/freecam.hpp"

#include "wz4lib/basic.hpp"
#include "wz4frlib/wz4_demo2.hpp"
#include "wz4frlib/wz4_demo2nodes.hpp"

#include "vorbisplayer.hpp"

#include "network.hpp"

#include "util/json.hpp"

#include "playlists.hpp"

/****************************************************************************/

static sDemoPackFile *PackFile=0;

#define PrepareFactor 16     // every 16 beats count as (Preparefactor) ops for the progressbar

/****************************************************************************/
/****************************************************************************/

void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)
  {
    sREGOPS(basic,0);
    sREGOPS(poc,1);
    sREGOPS(chaos_font,0);        // should go away soon (detuned)
    sREGOPS(wz3_bitmap,0);
    sREGOPS(wz4_anim,0);
    sREGOPS(wz4_mtrl,1);
    sREGOPS(chaosmesh,1);         // should go away soon (detuned)
    sREGOPS(wz4_demo2,0);
    sREGOPS(wz4_mesh,0);
    sREGOPS(chaosfx,0);
    sREGOPS(easter,0);
    sREGOPS(wz4_bsp,0);
    sREGOPS(wz4_mtrl2,0);
    sREGOPS(tron,1);
    sREGOPS(wz4_ipp,0);
    sREGOPS(wz4_audio,0);         // audio to image -> not usefull (yet)
    sREGOPS(fxparticle,0);
    sREGOPS(wz4_modmtrl,0);
    sREGOPS(wz4_modmtrlmod,0);

    sREGOPS(fr062,0);   // the cube
    sREGOPS(fr063_chaos,0);   // chaos+tron
    sREGOPS(fr063_tron,0);   // chaos+tron
    sREGOPS(fr063_mandelbulb,0);   // chaos+tron
    sREGOPS(fr063_sph,0);   // chaos+tron

    sREGOPS(adf,0);     
    sREGOPS(pdf,0);
  }

  Doc->FindType(L"Scene")->Secondary = 1;
  Doc->FindType(L"GenBitmap")->Order = 2;
  Doc->FindType(L"Wz4Mesh")->Order = 3;
  Doc->FindType(L"Wz4Render")->Order = 4;
  Doc->FindType(L"Wz4Mtrl")->Order = 5;

  wClass *cl = Doc->FindClass(L"MakeTexture2",L"Texture2D");
  sVERIFY(cl);
  Doc->Classes.RemOrder(cl);
  Doc->Classes.AddHead(cl);   // order preserving!
}

/****************************************************************************/
/****************************************************************************/

template <typename T> class sAutoPtr
{
public:
  sAutoPtr() : ptr(0) {}
  sAutoPtr(T* obj) { ptr = obj; }
  sAutoPtr(const sAutoPtr<T> &p) {sAddRef(ptr=p.ptr); }
  sAutoPtr& operator= (const sAutoPtr &p) { sRelease(ptr); sAddRef(ptr=p.ptr); return *this; }
  ~sAutoPtr() { sRelease(ptr); }
  T* operator -> () const { return ptr; }
  operator T*() const { return ptr; }

private:
  T* ptr;
};


sINITMEM(0,0);

class MyApp : public sApp
{
public:

  sString<1024> WZ4Name;

  sInt PreFrames;
  sInt Loaded;
  sString<256> ErrString;

  wOp *RootOp;
  sAutoPtr<Wz4Render> RootObj;

  sBool HasMusic;

  sInt StartTime;
  sInt Time;
  sPainter *Painter;
  sBool Fps;
  sBool MTMon;
  sInt FramesRendered;

  sRect ScreenRect;
  wPaintInfo PaintInfo;

  RPCServer *Server;

  PlaylistMgr PlMgr;

  sRandom Rnd;

  struct NamedObject
  {
    sString<64> Name;
    sAutoPtr<Wz4Render> Obj;
  };

  sArray<NamedObject> Transitions;

  wClass *CallClass;

  wOp *Tex1Op, *Tex2Op;
  sAutoPtr<Texture2D> Tex1, Tex2;
  sAutoPtr<Wz4Render> SlidePic[2];
  int CurPhase;

  sAutoPtr<Wz4Render> CurShow;
  
  sAutoPtr<Wz4Render> NextRender;
  sAutoPtr<Wz4Render> CurRender;
  sAutoPtr<Wz4Render> Main;

  sAutoPtr<Wz4Render> CurSlide;
  sAutoPtr<Wz4Render> NextSlide;

  sF32 TransTime, TransDurMS, CurRenderTime, NextRenderTime;

  MyApp()
  {
    Loaded=sFALSE;
    StartTime=0;
    Time=0;
    PreFrames=2;
    RootOp=0;
    RootObj=0;
    Fps = 0;
    MTMon = 0;
    Painter = new sPainter();
    FramesRendered = 0;
    CallClass = 0;
    CurSlide = 0;
    NextSlide = 0;
    TransTime = -1.0f;
    CurRenderTime = NextRenderTime = 0.0f;
    CurPhase = 0;

    new wDocument;
    sAddRoot(Doc);
    Doc->EditOptions.BackColor=0xff000000;

    Server = 0;
  }

  ~MyApp()
  {

    sDelete(Server);

    sRemRoot(Doc);
    delete Painter;

    if (PackFile)
    {
      sRemFileHandler(PackFile);
      sDelete(PackFile);
    }

  }


  wOp *MakeCall(const sChar *name, wOp *input)
  {
    if (!CallClass)
      CallClass = Doc->FindClass(L"Call",L"AnyType");

    wOp *callop = new wOp;
    callop->Class = CallClass;
    sU32 *flags = new sU32[1];
    flags[0] = 1;
    callop->EditData = flags;
    callop->Inputs.HintSize(2);
    callop->Inputs.AddTail(Doc->FindStore(name));
    callop->Inputs.AddTail(input);

    return callop;
  }

  void MakeNextSlide(sImageData &idata)
  {
    sTexture2D *tex = Tex1->Texture->CastTex2D();
    tex->ReInit(idata.SizeX,idata.SizeY,idata.Format);
    tex->LoadAllMipmaps(idata.Data);

    NextSlide = SlidePic[CurPhase];

    sSwap(Tex1Op, Tex2Op);
    sSwap(Tex1, Tex2);     
    CurPhase = 1-CurPhase;
  }

  void MakeNextSlide(const sChar *filename)
  {
    
    sImage img;
    img.Load(filename);
    sImageData idata(&img,sTEX_2D|sTEX_ARGB8888|sTEX_NOMIPMAPS);
    MakeNextSlide(idata);
   
  }

  void SetChild(const sAutoPtr<Wz4Render> &node, const sAutoPtr<Wz4Render> &child, sF32 *time)
  {
    sReleaseAll(node->RootNode->Childs);
    if (child)
    {
      child->RootNode->AddRef();
      node->RootNode->Childs.AddTail(child->RootNode);
      ((RNAdd*)node->RootNode)->TimeOverride = time;
    }
    else
    {
      ((RNAdd*)node->RootNode)->TimeOverride = 0;
    }
  }

  void SetTransition(int i, sF32 duration=1.0f)
  {
    const NamedObject &no = Transitions[i];
    SetChild(Main,no.Obj,&TransTime);
    SetChild(NextRender,NextSlide,&NextRenderTime);

    TransTime = 0.0f;
    TransDurMS = 1000*duration;
    NextRenderTime = 0.0f;
  }

  void EndTransition()
  {
    CurSlide = NextSlide;
    CurRenderTime = NextRenderTime;
    NextSlide = 0;
    TransTime = -1.0f;

    SetChild(CurRender,CurSlide,&CurRenderTime);
    SetChild(NextRender,0,0);
    SetChild(Main,CurShow,0);
  }

  void Load()
  {
    sInt t1=sGetTime();
    const sChar *rootname = L"root";
  
    RootOp = Doc->FindStore(rootname);
    if (!RootOp)
    {
      sExit();
      return;
    }
    RootObj=(Wz4Render*)Doc->CalcOp(RootOp);

    // the big test
    Tex1Op = Doc->FindStore(L"Tex1");
    Tex2Op = Doc->FindStore(L"Tex2");
    Tex1 = (Texture2D*)Doc->CalcOp(Tex1Op);
    Tex2 = (Texture2D*)Doc->CalcOp(Tex2Op);

    CurShow = (Wz4Render*)Doc->CalcOp(Doc->FindStore(L"CurShow"));
    CurRender = (Wz4Render*)Doc->CalcOp(Doc->FindStore(L"CurRender"));
    NextRender = (Wz4Render*)Doc->CalcOp(Doc->FindStore(L"NextRender"));
    Main = (Wz4Render*)Doc->CalcOp(Doc->FindStore(L"Main"));

    // load transitions
    wOp *op;
    sFORALL(Doc->Stores,op)
    {
      if (!sCmpStringILen(op->Name,L"trans_",6))
      {
        NamedObject no;
        no.Name = op->Name+6;
        no.Obj = (Wz4Render*)Doc->CalcOp(op);
        Transitions.AddTail(no);
      }
    }

    // load slide renderers
    SlidePic[0] = (Wz4Render*)Doc->CalcOp(MakeCall(L"slide_pic",Tex1Op));
    SlidePic[1] = (Wz4Render*)Doc->CalcOp(MakeCall(L"slide_pic",Tex2Op));

    MakeNextSlide(L"beamslide.png");
    EndTransition();

    Server = new RPCServer(PlMgr);

    Loaded=sTRUE;
  };


  void SetPaintInfo(wPaintInfo &pi)
  {
    pi.Window = 0;
    pi.Op = RootOp;

    pi.Lod = Doc->DocOptions.LevelOfDetail;


    pi.Env = 0;
    pi.Grid = 0;
    pi.Wireframe = 0;
    pi.CamOverride = 0;
    pi.Zoom3D = 1.0f;
  }


  void DoPaint(wObject *obj, wPaintInfo &pi)
  {
    sSetTarget(sTargetPara(sST_CLEARALL|sST_NOMSAA,0));

    sInt sx,sy;
    sGetRendertargetSize(sx,sy);
    sF32 scrnaspect=sGetRendertargetAspect();
    sF32 demoaspect=sF32(Doc->DocOptions.ScreenX)/sF32(Doc->DocOptions.ScreenY);
    sF32 arr=scrnaspect/demoaspect;
    sF32 w=0.5f, h=0.5f;
    if (arr>1) w/=arr; else h*=arr;
    ScreenRect.Init(sx*(0.5f-w)+0.5f,sy*(0.5f-h)+0.5f,sx*(0.5f+w)+0.5f,sy*(0.5f+h)+0.5f);

    pi.Client=ScreenRect;
    pi.Rect.Init();

    if(obj)
    {
      sTargetSpec spec(ScreenRect);
      sSetTarget(sTargetPara(sST_CLEARNONE,0,spec));

      sViewport view;
      view.SetTargetCurrent();
      view.Prepare();

      pi.CamOverride = sFALSE;

      pi.View = &view;
      pi.Spec = spec;

      obj->Type->BeforeShow(obj,pi);
      Doc->LastView = *pi.View;
      pi.SetCam = 0;
      Doc->Show(obj,pi);
    }
  }

  void OnPaint3D()
  {
    sSetTarget(sTargetPara(sST_CLEARALL,0));

    if (!Loaded)
    {
      // load and charge
      if (!PreFrames--)
      {
        PreFrames=0;

        sRender3DEnd();

        if (!Doc->Load(WZ4Name))
        {
          sFatal(L"could not load wz4 file %s",WZ4Name);
          return;
        }

        ProgressPaintFunc=0;

        Load();

        sRender3DBegin();

        PaintInfo.CacheWarmup = 0;
      }
    }

    if (Loaded)
    {      
      sInt tdelta;
      if (!StartTime) 
      {
          StartTime=sGetTime();
          Time = 0;
          tdelta = 0;
      }
      else
      {
        sInt newtime = sGetTime()-StartTime;
        tdelta = newtime-Time;
        Time = newtime;
      }

      NewSlideData *newslide = PlMgr.OnFrame(tdelta/1000.0f);

      if (newslide)
      {
        if (TransTime>=0)
          EndTransition();
        MakeNextSlide(*newslide->ImgData);
        if (newslide->TransitionTime>0)
        {
          if (newslide->TransitionId >= Transitions.GetCount())
            SetTransition(Rnd.Int(Transitions.GetCount()));
          else
            SetTransition(newslide->TransitionId);
        }
        else
          EndTransition();
        delete newslide;
      }

      SetPaintInfo(PaintInfo);

      PaintInfo.TimeMS = Time;
      PaintInfo.TimeBeat = Doc->MilliSecondsToBeats(Time);

      sTextBuffer Log;
      static sTiming time;

      time.OnFrame(sGetTime());
      if(Fps)
      {
        PaintInfo.ViewLog = &Log;

        sGraphicsStats stat;
        sGetGraphicsStats(stat);

        sString<128> b;

        sF32 ms = time.GetAverageDelta();
        Log.PrintF(L"'%f' ms %f fps\n",ms,1000/ms);
        Log.PrintF(L"'%d' batches '%k' verts %k ind %k prim %k splitters\n",
          stat.Batches,stat.Vertices,stat.Indices,stat.Primitives,stat.Splitter);
        sInt sec = PaintInfo.TimeMS/1000;
        Log.PrintF(L"Beat %d  Time %dm%2ds  Frames %d\n",PaintInfo.TimeBeat/0x10000,sec/60,sec%60,FramesRendered);
      }
      else
      {
        PaintInfo.ViewLog = 0;
      }
      PaintInfo.CamOverride = 0;

      DoPaint(RootObj,PaintInfo);
      FramesRendered++;

      if (TransTime>=0)
      {
        TransTime += tdelta/TransDurMS;
        if (TransTime>=1.0f)
          EndTransition();
      }
      CurRenderTime += tdelta/1000.0f;
      NextRenderTime += tdelta/1000.0f;

      sEnableGraphicsStats(0);
      sSetTarget(sTargetPara(0,0,&PaintInfo.Client));
      App2->Painter->SetTarget(PaintInfo.Client);
      App2->Painter->Begin();
      App2->Painter->SetPrint(0,0xff000000,1,0xff000000);
      App2->Painter->Print(10,11,Log.Get());
      App2->Painter->Print(11,10,Log.Get());
      App2->Painter->Print(12,11,Log.Get());
      App2->Painter->Print(11,12,Log.Get());
      App2->Painter->SetPrint(0,0xffc0c0c0,1,0xffffffff);
      App2->Painter->Print(11,11,Log.Get());
      App2->Painter->End();
      sEnableGraphicsStats(1);
      sSchedMon->FlipFrame();
      if(MTMon)
      {
        sTargetSpec ts;
        sSchedMon->Paint(ts);
      }
    }
  }

  void OnInput(const sInput2Event &ie)
  {

    if (PlMgr.OnInput(ie))
      return;

    sU32 key = ie.Key;
    key &= ~sKEYQ_CAPS;
    if(key & sKEYQ_SHIFT) key |= sKEYQ_SHIFT;
    if(key & sKEYQ_CTRL ) key |= sKEYQ_CTRL;
    switch(key)
    {
    case sKEY_ESCAPE|sKEYQ_SHIFT:
    case sKEY_F4|sKEYQ_ALT:
      sExit();
      break;

    case 'F'|sKEYQ_SHIFT:
      Fps = !Fps;
      break;

    case 'M'|sKEYQ_SHIFT:
      if(sSchedMon==0)
      {
        sSchedMon = new sStsPerfMon();
      }
      MTMon = !MTMon;
      break;

    }
  }
} *App2=0;


extern void sCollector(sBool exit=sFALSE);


static sBool LoadOptions(const sChar *filename, wDocOptions &options)
{
  sFile *f=sCreateFile(filename);
  if (!f) return sFALSE;

  sReader r;
  r.Begin(f);

  wDocument::SerializeOptions(r,options);
  sBool ret=r.End();

  delete f;
  return ret;
}


/****************************************************************************/
/****************************************************************************/

//{"rpc":{"@attributes":{"type":"request","name":"get_playlists"}}


static void sExitSts()
{
  sDelete(sSched);
}


void sMain()
{
  sAddSubsystem(L"StealingTaskScheduler (wz4player style)",0x80,0,sExitSts);

  sGetMemHandler(sAMF_HEAP)->MakeThreadSafe();

  sArray<sDirEntry> dirlist;
  sString<1024> wintitle;
  wintitle.PrintF(L"screens4");

  sSetWindowName(wintitle);
  sAddGlobalBlobHeap();

  //const sChar* str = L"{\"rpc\":{\"@attributes\":{\"type\":\"request\",\"name\":\"get_playlists\"}}}";
 
//  sJSON json;
//  sJSON::Object *obj = json.Parse(str)->As<sJSON::Object>();
//  sJSON::Object *rpc = obj->Items.Get(L"rpc")->As<sJSON::Object>();
//  const sChar *type = rpc->Attributes.Get(L"type")->As<sJSON::String>()->Value;
//  const sChar  *name = rpc->Attributes.Get(L"name")->As<sJSON::String>()->Value;
  
//  delete obj;
//  return;

  // find packfile and open it
  {
    sString<1024> pakname;
    sString<1024> programname;
    const sChar *cmd=sGetCommandLine();
    while (sIsSpace(*cmd) || *cmd=='"') cmd++;
    const sChar *pstart=cmd;
    while (*cmd && !(sIsSpace(*cmd) || *cmd=='"')) cmd++;
    sCopyString(programname,pstart,cmd-pstart+1);
    
    sCopyString(sFindFileExtension(programname),L"pak",4);
    if (sCheckFile(programname))
      pakname=programname;
    else
    {
      // use first packfile from current directory
      sLoadDir(dirlist,L".",L"*.pak");
      if (!dirlist.IsEmpty())
        pakname=dirlist[0].Name;       
      dirlist.Clear();
    }

    if (!pakname.IsEmpty())
    {
      PackFile = new sDemoPackFile(pakname);
      sAddFileHandler(PackFile);
    }
  }

  // find fitting wz4 file
  sString<1024> wz4name;

  const sChar *fromcmd=sGetShellParameter(0,0); // command line?
  if (fromcmd && sCheckFile(fromcmd))
    wz4name=fromcmd;
  else
  {
    const sChar *fromtxt=sLoadText(L"wz4file.txt"); // text file with name in it? (needed for packfile)
    if (fromtxt) 
    {
      wz4name=fromtxt;
      sDeleteArray(fromtxt);
    }
    else
    {
      sLoadDir(dirlist,L".",L"*.wz4"); // wz4 file in current directory?
      if (!dirlist.IsEmpty())
        wz4name=dirlist[0].Name;       
      dirlist.Clear();
    }
  }
  if (wz4name.IsEmpty())
  {
    sFatal(L"no .suitable .wz4 file found");
  }
  
  // try to load doc options from wz4 file
  wDocOptions opt;
  LoadOptions(wz4name,opt);

  // get demo title from doc options
  sString<256> title=sGetWindowName();
  if (!opt.ProjectName.IsEmpty())
  {
    if (opt.ProjectId>0)
      title.PrintF(L"fr-0%d: %s",opt.ProjectId,opt.ProjectName);
    else if (opt.ProjectId<0)
      title.PrintF(L"fr-minus-0%d: %s",-opt.ProjectId,opt.ProjectName);
    else
      title.PrintF(L"%s",opt.ProjectName);
    sSetWindowName(title);
  }

  // open selector and set screen mode
  sTextBuffer tb;
  sInt flags=sISF_2D|sISF_3D|sISF_CONTINUOUS; // need 2D for text rendering

 
  title.PrintAddF(L"  (player V%d.%d)",WZ4_VERSION,WZ4_REVISION);
  if(sCONFIG_64BIT)
    title.PrintAddF(L" (64Bit)");
  sSetWindowName(title);

//  sCheckCapsHook->Add(CheckCapsCallback);   // for fr-062 vertex texture check


  sScreenMode sm;
  sm.Aspect = (float)opt.ScreenX/(float)opt.ScreenY;
  sm.Display = -1;
  sm.Flags = 0;
#if sRELEASE
  flags|=sISF_FULLSCREEN;
  sm.Flags |= sSM_FULLSCREEN;
#endif
  sm.Frequency = 0;
  sm.MultiLevel = 0;
  sm.OverMultiLevel = 0;
  sm.OverX = sm.ScreenX = opt.ScreenX;
  sm.OverY = sm.ScreenY = opt.ScreenY;
  sm.RTZBufferX = 2048;
  sm.RTZBufferY = 2048;

  sSetScreenMode(sm);
  sInit(flags,sm.ScreenX,sm.ScreenY);

  sSched = new sStsManager(128*1024,512,0);

  // .. and go!
  App2=new MyApp;
  App2->WZ4Name = wz4name;
  sSetApp(App2);
}

/****************************************************************************/