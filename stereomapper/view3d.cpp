#include <iostream>
#include "view3d.h"
#include <math.h>
#include "../libviso2/src/matrix.h"

#define DEBUG_SHOW_HOMOGRAPHY_C 0

using namespace std;

View3D::View3D(QWidget *parent) :
    QGLWidget(QGLFormat(QGL::SampleBuffers),parent)
{
    _pose_curr.zoom = -1.5;
    _pose_curr.rotx = 180;
    _pose_curr.roty = 0;
    _pose_curr.tx   = 0;
    _pose_curr.ty   = 0;
    _pose_curr.tz   = -1.5;
    _bg_wall_flag   = 0;
    _bg_wall_pos    = 1;
    _show_cam_flag  = 1;
    _show_grid_flag  = 1;
    _show_white_flag  = 0;
}

//==============================================================================//

View3D::~View3D()
{
    clearAll();
}

//==============================================================================//

void View3D::mousePressEvent(QMouseEvent *event)
{
    _last_pos = event->pos();
}

//==============================================================================//

void View3D::mouseMoveEvent(QMouseEvent *event)
{
    // compute deltas
    float dx = -event->x() + _last_pos.x();
    float dy = +event->y() - _last_pos.y();

    // both buttons => zoom
    if (event->buttons() & Qt::MidButton)
    {
        if (dy>0)
        {
            _pose_curr.zoom *= (1+0.01*fabs(dy));
        }
        else
        {
            _pose_curr.zoom /= (1+0.01*fabs(dy));
        }
        updateGL();
    }
    // left button => rotation
    else if (event->buttons() & Qt::LeftButton)
    {
        _pose_curr.rotx += dy;
        if (_pose_curr.rotx<90)
        {
            _pose_curr.rotx = 90;
        }
        if (_pose_curr.rotx>270)
        {
            _pose_curr.rotx = 270;
        }
        _pose_curr.roty += dx;

        if (_pose_curr.roty<-180)
        {
            _pose_curr.roty = -180;
        }
        if (_pose_curr.roty>+180)
        {
            _pose_curr.roty = +180;
        }
    }
    // right button => translation
    else if (event->buttons() & Qt::RightButton)
    {
        float rotx2 = _pose_curr.rotx;
        if (rotx2<170) rotx2 = 90;
        if (rotx2>190) rotx2 = 270;
        float rx = rotx2*M_PI/180.0;
        float ry = _pose_curr.roty*M_PI/180.0;

        Matrix R = Matrix::rotMatY(-ry)*Matrix::rotMatX(-rx);

        Matrix v(3,1);
        v._val[0][0] = dx;
        v._val[1][0] = dy;

        v = R*v;
        _pose_curr.tx += 0.0025*_pose_curr.zoom*v._val[0][0];
        _pose_curr.ty += 0.0025*_pose_curr.zoom*v._val[1][0];
        _pose_curr.tz += 0.0025*_pose_curr.zoom*v._val[2][0];
    }

    _last_pos = event->pos();

    updateGL();
}

//==============================================================================//

void View3D::wheelEvent(QWheelEvent *event)
{
    if (event->delta()>0)
    {
        _pose_curr.zoom *= 1.1;
    }
    else
    {
        _pose_curr.zoom /= 1.1;
    }
    updateGL();
}

//==============================================================================//

void View3D::addCamera(Matrix H_total, float s, bool keyframe)
{
    // create list with points for this camera
    Matrix C(4,10); // the visualized red rectangle with the cross has 10 points.
    C._val[0][0] = -0.5*s; C._val[1][0] = -0.5*s; C._val[2][0] = +1.0*s;
    C._val[0][1] = +0.5*s; C._val[1][1] = -0.5*s; C._val[2][1] = +1.0*s;
    C._val[0][2] = +0.5*s; C._val[1][2] = +0.5*s; C._val[2][2] = +1.0*s;
    C._val[0][3] = -0.5*s; C._val[1][3] = +0.5*s; C._val[2][3] = +1.0*s;
    C._val[0][4] = -0.5*s; C._val[1][4] = -0.5*s; C._val[2][4] = +1.0*s;
    C._val[0][5] =      0; C._val[1][5] =      0; C._val[2][5] =      0;
    C._val[0][6] = +0.5*s; C._val[1][6] = -0.5*s; C._val[2][6] = +1.0*s;
    C._val[0][7] = +0.5*s; C._val[1][7] = +0.5*s; C._val[2][7] = +1.0*s;
    C._val[0][8] =      0; C._val[1][8] =      0; C._val[2][8] =      0;
    C._val[0][9] = -0.5*s; C._val[1][9] = +0.5*s; C._val[2][9] = +1.0*s;

    for (int32_t i=0; i<10; i++)
    {
        C._val[3][i] = 1;
    }

    // transfer camera to reference coordinate system
    Matrix C_ref = H_total*C;

 #if DEBUG_SHOW_HOMOGRAPHY_C
    std::cout << "C\n" << C << "\n=====\n";
    std::cout << "H_total\n" << H_total << "\n=====\n";
    std::cout << "C_ref\n" << C_ref << "\n=====\n";
#endif

    // add camera to list of cameras
    cam ccam;
    ccam.keyframe = keyframe;
    for (int32_t i=0; i<10; i++)
    {
        for (int32_t j=0; j<3; j++)
        {
            ccam.p[i][j] = C_ref._val[j][i];
        }
    }
    _cams.push_back(ccam);

    // update 3d view
    updateGL();
}

//==============================================================================//

void View3D::addPoints(std::vector<std::vector<point_3d>> p)
{
    makeCurrent();

    // if 2 point clouds given, delete last display list
    if (p.size()>1 && _gl_list.size()>0)
    {
        glDeleteLists(_gl_list.back(),1);
        _gl_list.pop_back();
    }

    // process last two elements of p
    for (int32_t i=max((int32_t)p.size()-2,0); i<(int32_t)p.size(); i++)
    {
        if (0)
        {
            // allocate bins
            vector<vector<point_3d>> points;
            for (int32_t j=0; j<256; j++)
            {
                points.push_back(vector<point_3d>());
            }

            // put points into bins
            for (vector<point_3d>::iterator it=p[i].begin(); it!=p[i].end(); it++)
            {
                points[(int32_t)(it->val*255.0)].push_back(*it);
            }

            // create display list
            int32_t gl_idx = glGenLists(1);
            _gl_list.push_back(gl_idx);
            glNewList(gl_idx,GL_COMPILE);

            glPointSize(2);
            glBegin(GL_POINTS);
            for (int32_t j=0; j<256; j++)
            {
                if (points[j].size()>0)
                {
                    float c = (float)j/255.0;
                    glColor3f(c,c,c);
                    for (vector<point_3d>::iterator it=points[j].begin(); it!=points[j].end(); it++)
                    {
                        glVertex3f(it->x,it->y,it->z);
                    }
                }
            }

            // finish display list
            glEnd();
            glEnd();
            glEndList();
        }
        else
        {
            // create display list
            int32_t gl_idx = glGenLists(1);
            _gl_list.push_back(gl_idx);
            glNewList(gl_idx,GL_COMPILE);

            glPointSize(2);
            glBegin(GL_POINTS);
            for (vector<point_3d>::iterator it=p[i].begin(); it!=p[i].end(); it++)
            {
                glColor3f(it->val,it->val,it->val);
                glVertex3f(it->x,it->y,it->z);
            }

            // finish display list
            glEnd();
            glEnd();
            glEndList();
        }
    }

    doneCurrent();

    // update view
    updateGL();
}

//==============================================================================//

void View3D::initializeGL()
{
    makeCurrent();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);

    doneCurrent();
}

//==============================================================================//

void View3D::paintGL()
{
    makeCurrent();

    // clear screen & set matrices
    if (_show_white_flag)
    {
        glClearColor(1,1,1,1);
    }
    else
    {
        glClearColor(0,0,0,1);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    // apply transformation
    glTranslatef(0.0, 0.0,_pose_curr.zoom);
    glRotatef(_pose_curr.rotx, 1.0, 0.0, 0.0);
    glRotatef(_pose_curr.roty, 0.0, 1.0, 0.0);
    glTranslatef(_pose_curr.tx,_pose_curr.ty,_pose_curr.tz);

    if (_show_grid_flag)
    {
        // paint ground grid
        float r = 200;
        float h = 2;
        glColor3f(0.5,0.5,0.5);
        glLineWidth(1);
        glBegin(GL_LINES);
        for (float x=-r; x<=r+0.001; x+=5)
        {
            glVertex3f(x,h,-r); glVertex3f(x,h,+r);
            glVertex3f(-r,h,x); glVertex3f(+r,h,x);
        }
        glEnd();
    }

    // draw 3d points
    for (vector<GLuint>::iterator it=_gl_list.begin(); it!=_gl_list.end(); it++)
    {
        glCallList(*it);
    }

    // draw background wall
    if (_bg_wall_flag)
    {
        if (_show_white_flag)
        {
            glColor3f(1,1,1);
        }
        else
        {
            glColor3f(0,0,0);
        }

        glBegin(GL_QUADS);
        glVertex3f(+20,-20,_bg_wall_pos);
        glVertex3f(-20,-20,_bg_wall_pos);
        glVertex3f(-20,+20,_bg_wall_pos);
        glVertex3f(+20,+20,_bg_wall_pos);
        glEnd();
    }

    if (_show_cam_flag)
    {
        // draw cameras
        glDisable(GL_DEPTH_TEST);
        glLineWidth(1);
        for (vector<cam>::iterator it = _cams.begin(); it!=_cams.end(); it++)
        {
            if (it->keyframe) glColor3f(1.0,0.0,0.0);
            else              glColor3f(1.0,1.0,0.0);

            glBegin(GL_LINE_STRIP);
            for (int32_t i=0; i<10; i++)
            {
                glVertex3f(it->p[i][0],it->p[i][1],it->p[i][2]);
            }
            glEnd();
        }

        // draw connection line
        glBegin(GL_LINE_STRIP);
        for (vector<cam>::iterator it = _cams.begin(); it!=_cams.end(); it++)
        {
            glVertex3f(it->p[5][0],it->p[5][1],it->p[5][2]);
        }
        glEnd();
        glEnable(GL_DEPTH_TEST);


        // draw coordinate system
        float s = 0.3;
        glDisable(GL_DEPTH_TEST);
        glLineWidth(3);
        glBegin(GL_LINES);
        glColor3f(1,0,0); glVertex3f(0,0,0); glVertex3f(s,0,0);
        glColor3f(0,1,0); glVertex3f(0,0,0); glVertex3f(0,s,0);
        glColor3f(0,0,1); glVertex3f(0,0,0); glVertex3f(0,0,s);
        glEnd();
        glEnable(GL_DEPTH_TEST);


        // draw rotation anchor
        glPointSize(3);
        glColor3f(1.0,0.0,0.0);
        glBegin(GL_POINTS);
        glVertex3f(-_pose_curr.tx,-_pose_curr.ty,-_pose_curr.tz);
        glEnd();
    }

    doneCurrent();
}

//==============================================================================//

void View3D::resizeGL(int width, int height)
{
    makeCurrent();
    int side = qMax(width, height);
    glViewport((width-side)/2,(height-side)/2,side,side);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45,1,0.1,10000);
    glMatrixMode(GL_MODELVIEW);

    doneCurrent();
}

//==============================================================================//

void View3D::recordHuman()
{
    _poses.clear();
    pose pose1 = _pose_curr;
    pose pose2 = _pose_curr;
    pose1.roty -= 45;
    pose2.roty += 45;
    _poses.push_back(pose1);
    _poses.push_back(pose2);
    _poses.push_back(pose1);
    playPoses(1);
}

//==============================================================================//

void View3D::playPoses(bool record)
{
    string dir;
    if(record)
    {
        dir = createNewRecordDirectory();
    }
    float step_size = 0.02;
    int k1=0;
    int k2=0;
    for (int i=0; i<(int)_poses.size()-1; i++)
    {
        pose pose1 = _poses[i];
        pose pose2 = _poses[i+1];
        for (float pos=0; pos<=1; pos+=step_size)
        {
            float pos2 = (1+sin(-M_PI/2+pos*M_PI))/2;
            _pose_curr.zoom = pose1.zoom+(pose2.zoom-pose1.zoom)*pos2;
            _pose_curr.rotx = pose1.rotx+(pose2.rotx-pose1.rotx)*pos2;
            _pose_curr.roty = pose1.roty+(pose2.roty-pose1.roty)*pos2;
            _pose_curr.tx   = pose1.tx  +(pose2.tx  -pose1.tx  )*pos2;
            _pose_curr.ty   = pose1.ty  +(pose2.ty  -pose1.ty  )*pos2;
            _pose_curr.tz   = pose1.tz  +(pose2.tz  -pose1.tz  )*pos2;
            updateGL();
            if (record)
            {
                QImage img_320_480 = this->grabFrameBuffer();
                char filename[1024];
                sprintf(filename,"%simg_320_480_%06d.png",dir.c_str(),k1++);
                cout << "Storing " << filename << endl;
                img_320_480.save(QString(filename));
                if (k1%3==0)
                {
                    QImage img_160_240 = img_320_480.scaled(160,240,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                    sprintf(filename,"%simg_160_240_%06d.png",dir.c_str(),k2++);
                    cout << "Storing " << filename << endl;
                    img_160_240.save(QString(filename));
                }
            }
        }
    }
    // Comment out because there is no upload.sh
    //char command[1024];
    //sprintf(command,"gnome-terminal -e \"./upload.sh %s\"",dir.c_str());
    //system(command);
}

//==============================================================================//

string View3D::createNewRecordDirectory()
{
    char buffer[1024];
    int system_status = 0;

    for (int32_t i=0; i<9999; i++)
    {
        sprintf(buffer,"/home/geiger/4_Projects/stereomapper/record_%04d/img_320_480_000000.png",i);
        FILE *file = fopen (buffer,"r");
        if (file!=NULL)
        {
            fclose (file);
            continue;
        }
        sprintf(buffer,"/home/geiger/4_Projects/stereomapper/record_%04d/",i);
        break;
    }
    cout << "Creating record directory: " << buffer << endl;
    char cmd[1024];
    sprintf(cmd,"mkdir %s",buffer);

    // Do nothing. Just for resolving the warning message.
    system_status = system(cmd);
    if( system_status ) {};

    return buffer;
}
