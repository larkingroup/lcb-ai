#ifndef LTS_LAYOUT_H
#define LTS_LAYOUT_H
typedef struct PaneLayout { int left, center, right, rightx, bottom, composer; } PaneLayout;
static inline PaneLayout pane_layout(int width,int height)
{
	PaneLayout p;
	p.left=182; p.right=258; p.rightx=width-p.right-4;
	p.center=p.rightx-p.left-10; p.bottom=height-128;
	p.composer=p.bottom-108; return p;
}
static inline int layout_clamp(int n,int low,int high) { return n<low?low:n>high?high:n; }
static inline PaneLayout workbench_layout(int width,int height,int left,int right,int output)
{
    PaneLayout p;
    p.left=left?layout_clamp(left,160,width-556):0;
    p.right=right?layout_clamp(right,220,width-p.left-336):0;
    p.rightx=width-p.right-4;
    p.center=p.rightx-p.left-10;
    p.bottom=height-(output?layout_clamp(output,100,height-420):24);
    p.composer=p.bottom-108;
    return p;
}
#endif
