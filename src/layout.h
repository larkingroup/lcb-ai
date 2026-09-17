#ifndef LTI_LAYOUT_H
#define LTI_LAYOUT_H
typedef struct PaneLayout { int left, center, right, rightx, bottom, composer; } PaneLayout;
static inline PaneLayout pane_layout(int width,int height)
{
	PaneLayout p;
	p.left=182; p.right=258; p.rightx=width-p.right-4;
	p.center=p.rightx-p.left-10; p.bottom=height-128;
	p.composer=p.bottom-108; return p;
}
#endif
