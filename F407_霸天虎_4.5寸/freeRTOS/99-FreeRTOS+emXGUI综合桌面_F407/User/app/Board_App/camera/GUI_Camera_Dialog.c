#include <emXGUI.h>
#include <string.h>
#include "Widget.h"
#include "board.h"
#include "./camera/bsp_ov5640.h"
#include "x_libc.h"
#include "./camera/ov5640_AF.h"
#include "sdio/bsp_sdio_sd.h"

OV5640_IDTypeDef OV5640_Camera_ID;
TaskHandle_t h_autofocus;
BOOL update_flag = 0;//帧率更新标志
uint8_t fps=0;//帧率
HWND Cam_hwnd;//主窗口句柄
static SURFACE *pSurf;
GUI_SEM *cam_sem = NULL;//更新图像同步信号量（二值型）
static uint8_t OV5640_State = 0;

/*
 * @brief  更新屏幕
 * @param  NONE
 * @retval NONE
*/
static void Update_Dialog()
{

	while(1) //线程已创建了
	{
    GUI_SemWait(cam_sem, 0xFFFFFFFF);

    fps ++;                         // 帧率自加

    InvalidateRect(Cam_hwnd,NULL,FALSE);
    
    OV5640_State = 2;
	}
}
extern int SelectDialogBox(HWND hwndParent, RECT rc,const WCHAR *pText,const WCHAR *pCaption,const MSGBOX_OPTIONS *ops);
/*
 * @brief  摄像头窗口回调函数
*/
static LRESULT WinProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  static int old_fps = 0;

  switch(msg)
  {
    case WM_CREATE:
    {
      //* 初始化摄像头GPIO及IIC */
      OV5640_HW_Init();  
      /* 读取摄像头芯片ID，确定摄像头正常连接 */
      OV5640_ReadID(&OV5640_Camera_ID);
      OV5640_State = 1;
      
      if(OV5640_Camera_ID.PIDH  == 0x56)
      {
        GUI_DEBUG("OV5640 ID:%x %x",OV5640_Camera_ID.PIDH ,OV5640_Camera_ID.PIDL);
        OV5640_State = 0;
        
        cam_sem = GUI_SemCreate(0,1);//同步摄像头图像
      
      //创建自动对焦线程
      xTaskCreate((TaskFunction_t )(void(*)(void*))Update_Dialog,  /* 任务入口函数 */
                            (const char*    )"Update_Dialog",/* 任务名字 */
                            (uint16_t       )1*1024/2,  /* 任务栈大小FreeRTOS的任务栈以字为单位 */
                            (void*          )NULL,/* 任务入口函数参数 */
                            (UBaseType_t    )15, /* 任务的优先级 */
                            (TaskHandle_t  )&h_autofocus);/* 任务控制块指针 */
      }
      else
      {
        OV5640_State = 1;     // 没有检测到摄像头
        SetTimer(hwnd, 3, 3, TMR_START | TMR_SINGLE, NULL);      // 初始化出错启动提示
        break;
      }
			SetTimer(hwnd,2,20,TMR_START,NULL);
			
      break;  
    }
    case WM_LBUTTONDOWN://点击屏幕，关闭窗口
    {
      
      PostCloseMessage(hwnd);
  
      break;
    }
    case WM_TIMER://摄像头状态机
    {
      update_flag = 1;
      if(OV5640_State == 1)
      
      {
        /* 初始化失败 */
        RECT RC;
        MSGBOX_OPTIONS ops;
        const WCHAR *btn[] = { L"确认",L"取消" };      //对话框内按钮的文字

        ops.Flag = MB_ICONERROR;
        ops.pButtonText = btn;
        ops.ButtonCount = 2;
        RC.w = 300;
        RC.h = 200;
        RC.x = (GUI_XSIZE - RC.w) >> 1;
        RC.y = (GUI_YSIZE - RC.h) >> 1;
        SelectDialogBox(hwnd, RC, L"没有检测到ov5640摄像头\n请重新检查连接。", L"错误", &ops);    // 显示错误提示框
        PostCloseMessage(hwnd);
      }
      
			else if(OV5640_State == 0)
			{
					OV5640_Init();  
					OV5640_RGB565Config();
					OV5640_USER_Config();
					OV5640_FOCUS_AD5820_Init();

					if(cam_mode.auto_focus ==1)
					{
					OV5640_FOCUS_AD5820_Constant_Focus();
					//              focus_status = 1;
					}
					//使能DCMI采集数据
          OV5640_Capture_Control(ENABLE);

					OV5640_State = 2;
					InvalidateRect(hwnd, NULL, TRUE);

			}
      break;
    }
    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC hdc;
      RECT rc;
      
      hdc = BeginPaint(hwnd,&ps);
      GetClientRect(hwnd,&rc);
      if(OV5640_State != 2)
      {
        SetTextColor(hdc,MapRGB(hdc,250,250,250));
        SetBrushColor(hdc,MapRGB(hdc,50,0,0));
        SetPenColor(hdc,MapRGB(hdc,250,0,0));
        DrawText(hdc,L"正在初始化摄像头\r\n\n请等待...",-1,&rc,DT_VCENTER|DT_CENTER|DT_BKGND);
      }              
      if(OV5640_State == 2)
      {   
				OV5640_Capture_Control(ENABLE);
      }

      EndPaint(hwnd,&ps);
      break;
    }

    case WM_DESTROY:
    {
      old_fps = 0;
      fps = 0;
      
      if (OV5640_State != 1)
      {
        GUI_SemDelete(cam_sem);
        DeleteSurface(pSurf);
        GUI_Thread_Delete(h_autofocus);
      }
			OV5640_Reset();
      OV5640_Capture_Control(DISABLE);//关闭摄像头采集图像
      DMA_ITConfig(DMA2_Stream1,DMA_IT_TC,DISABLE); //关闭DMA中断
      DCMI_Cmd(DISABLE); //DCMI失能
      DCMI_CaptureCmd(DISABLE); 
 
      return PostQuitMessage(hwnd);	
    }
    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
  return WM_NULL;
}


void	GUI_Camera_DIALOG(void)
{	
	WNDCLASS	wcex;
	MSG msg;

	wcex.Tag = WNDCLASS_TAG;  

	wcex.Style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WinProc; //设置主窗口消息处理的回调函数.
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = NULL;//hInst;
	wcex.hIcon = NULL;//LoadIcon(hInstance, (LPCTSTR)IDI_WIN32_APP_TEST);
	wcex.hCursor = NULL;//LoadCursor(NULL, IDC_ARROW);

	//创建主窗口
	Cam_hwnd = CreateWindowEx(WS_EX_NOFOCUS|WS_EX_FRAMEBUFFER,
                            &wcex,
                            L"GUI_Camera_Dialog",
                            WS_VISIBLE|WS_CLIPCHILDREN|WS_OVERLAPPED,
                            0, 0, GUI_XSIZE, GUI_YSIZE,
                            NULL, NULL, NULL, NULL);

	//显示主窗口
	ShowWindow(Cam_hwnd, SW_SHOW);

	//开始窗口消息循环(窗口关闭并销毁时,GetMessage将返回FALSE,退出本消息循环)。
	while (GetMessage(&msg, Cam_hwnd))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
  }
	//由于摄像头和SD卡引脚复用，退出时重新初始化文件系统
  if(FileSystem_Init() != TRUE)
  {
    GUI_ERROR("File_System Failed.");
  }
}
