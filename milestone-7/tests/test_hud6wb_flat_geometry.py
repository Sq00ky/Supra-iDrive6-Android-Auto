"""Independent numerical model of flat road compositions, not C++ execution."""
import math
import pathlib
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'src/cluster/grafite_oro/artwork6wb.cpp').read_text()

def roadpath(direction,uturn=False):
    if uturn:
        side=-1 if direction<0 else 1
        return [(500-side*250,900),(500-side*250,350)]+[(500-side*250*math.cos(math.pi*i/16),350-250*math.sin(math.pi*i/16))for i in range(1,17)]+[(500+side*250,650)]
    if not direction:return [(500,900),(500,100)]
    out=[(500,900),(500,560)]
    if abs(direction)in(90,135):
        side=-1 if direction<0 else 1;steps=8 if abs(direction)==90 else 12;turn=math.radians(abs(direction))
        out += [(500+side*220*(1-math.cos(turn*i/steps)),560-220*math.sin(turn*i/steps))for i in range(1,steps+1)]
        x,y=out[-1];out.append((x+side*math.sin(turn)*330,y-math.cos(turn)*330))
    else:out.append((500+math.sin(math.radians(direction))*500,560-math.cos(math.radians(direction))*500))
    return out

def silhouette(path,half,head):
    clean=[]
    for p in path:
        if not clean or abs(p[0]-clean[-1][0])>1e-9 or abs(p[1]-clean[-1][1])>1e-9:clean.append(p)
    path=clean
    left=[];right=[]
    for i,p in enumerate(path):
        a,b=(path[i-1],p)if i else(path[0],path[1]);dx,dy=b[0]-a[0],b[1]-a[1];n=math.hypot(dx,dy);before=(dx/n,dy/n)
        a,b=(p,path[i+1])if i+1<len(path)else(path[-2],path[-1]);dx,dy=b[0]-a[0],b[1]-a[1];n=math.hypot(dx,dy);after=(dx/n,dy/n)
        nx,ny=-before[1]-after[1],before[0]+after[0];n=math.hypot(nx,ny)
        nx,ny=(-after[1],after[0])if n<1e-9 else(nx/n,ny/n)
        distance=half/max(.5,nx*(-after[1])+ny*after[0]);nx*=distance;ny*=distance
        left.append((p[0]+nx,p[1]+ny));right.append((p[0]-nx,p[1]-ny))
    out=left
    if head:
        x,y=path[-1];a,b=path[-2];dx,dy=x-a,y-b;n=math.hypot(dx,dy);dx/=n;dy/=n
        out += [(x-dy*half*2.3,y+dx*half*2.3),(x+dx*half*2.5,y+dy*half*2.5),(x+dy*half*2.3,y-dx*half*2.3)]
    return out+right[::-1]

def composition(direction,uturn=False,branch=False,merge=0):
    paths=[]
    if branch:paths.append((roadpath(35 if direction<0 else -35),180,'road'))
    if merge:
        paths.append(([(500+(-1 if merge<0 else 1)*520,900),(500,500),(500,100)],180,'road'))
        if merge==2:paths.append(([(1020,900),(500,500),(500,100)],180,'road'))
    route=roadpath(direction,uturn);paths.append((route,120 if uturn else 180 if direction else 310,'road'))
    arrow=route.copy();x,y=arrow[-1];a,b=arrow[-2];dx,dy=x-a,y-b;n=math.hypot(dx,dy);half=45 if uturn else 60;arrow[-1]=(x-dx/n*half*2.5,y-dy/n*half*2.5)
    paths.append((arrow,45 if uturn else 60,'arrow'))
    return [(silhouette(p,h,role=='arrow'),role)for p,h,role in paths]

class FlatGeometryChecks(unittest.TestCase):
    def test_all_road_categories_and_no_zero_edges(self):
        fixtures=[(0,False,False,0),(-35,False,True,0),(35,False,True,0)]
        fixtures += [(d,False,b,0)for d in(-45,45,-90,90,-135,135)for b in(False,True)]
        fixtures += [(-1,True,False,0),(1,True,False,0),(0,False,False,-1),(0,False,False,1),(0,False,False,2)]
        for fixture in fixtures:
            shapes=composition(*fixture)
            self.assertEqual(sum(role=='arrow'for p,role in shapes),1)
            for p,role in shapes:
                self.assertTrue(all(math.isfinite(v)for pt in p for v in pt))
                self.assertGreaterEqual(len(p),4)
                area=sum(p[j][0]*p[(j+1)%len(p)][1]-p[(j+1)%len(p)][0]*p[j][1]for j in range(len(p)))
                self.assertNotEqual(area,0)
                def cross(a,b,c):return(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
                for j in range(len(p)):
                    a,b=p[j],p[(j+1)%len(p)]
                    for k in range(j+2,len(p)):
                        if (k+1)%len(p)==j:continue
                        c,d=p[k],p[(k+1)%len(p)]
                        self.assertFalse(cross(a,b,c)*cross(a,b,d)<-1e-8 and cross(c,d,a)*cross(c,d,b)<-1e-8)

    def test_shared_curve_endpoints_do_not_erase_guidance(self):
        path=[(500,900),(500,500),(500,500),(600,400),(600,400),(800,400)]
        self.assertEqual(silhouette(path,28,True),silhouette([path[i]for i in(0,1,3,5)],28,True))
        self.assertIn('fabs(input[i].x-path.back().x)>1e-9',SOURCE)
        self.assertIn('fabs(input[i].y-path.back().y)>1e-9',SOURCE)

    def test_complete_bounds_include_contours_and_uniform_scale(self):
        for d,u in[(0,False),(-35,False),(35,False),(-45,False),(45,False),(-90,False),(90,False),(-135,False),(135,False),(-1,True),(1,True)]:
            shapes=composition(d,u,not u and bool(d));points=[pt for p,role in shapes for pt in p]
            l=min(x-3 for x,y in points);r=max(x+3 for x,y in points);t=min(y-3 for x,y in points);b=max(y+3 for x,y in points)
            scale=min(232/(r-l),205/(b-t));dx=(232-(l+r)*scale)/2;dy=(205-(t+b)*scale)/2
            for x,y in points:
                self.assertTrue(-1e-6<=x*scale+dx-3*scale<=232)
                self.assertTrue(-1e-6<=y*scale+dy-3*scale<=205)
                self.assertLessEqual(x*scale+dx+3*scale,232+1e-6)
                self.assertLessEqual(y*scale+dy+3*scale,205+1e-6)
            self.assertTrue(abs((r-l)*scale-232)<1e-6 or abs((b-t)*scale-205)<1e-6)

    def test_mirrored_turns_keep_proportions(self):
        for direction in(35,45,90,135):
            left=roadpath(-direction);right=roadpath(direction)
            for a,b in zip(left,right):self.assertAlmostEqual(a[0]+b[0],1000);self.assertAlmostEqual(a[1],b[1])
        for i,(x,y)in enumerate(roadpath(1,True)[1:-1]):
            self.assertAlmostEqual(math.hypot(x-500,y-350),250)

    def test_design_constants_match_source_and_all_symbols_keep_catalogue(self):
        for term in('half=uturn?120:direction?180:310','guidance(arrow,uturn?45:60)',
                     'normal.x*(-after.y)+normal.y*after.x','std::max(0.5,cosine)',
                     'else {for(unsigned i=0;i<a.count;++i)scene.push_back(load(a.offset+i));symbolStyle(scene);}'):self.assertIn(term,SOURCE)
        self.assertIn('Color::Graphite,Color::Gold,6',SOURCE)
        self.assertIn('Color::White,Color::Red,6',SOURCE)

if __name__=='__main__':unittest.main()
