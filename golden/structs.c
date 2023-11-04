int f(int x, int y) {
  struct x {  // hehehehe
    int x, y;  // note the names!
    struct {
      int u;
      int v;
    };
  };
  struct x a;
  struct x b;
  a.x = x;
  a.y = y;
  a.u = 10;
  a.v = 20;
  
  b.x = 3 * y;
  b.y = 5 * x;
  b.u = 100;
  b.u = 200;

  return a.x + a.y + a.u + a.v + b.x + b.y + b.u + b.v;
}
