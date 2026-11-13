// C++ runtime check: containers, streams, virtual functions, exceptions.
#include <algorithm>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Shape {
  virtual ~Shape() {}
  virtual double area() const = 0;
  virtual std::string name() const { return "shape"; }
};
struct Rect : Shape {
  double w, h;
  Rect(double w_, double h_) : w(w_), h(h_) {}
  double area() const { return w * h; }
  std::string name() const { return "rect"; }
};
struct Square : Rect {
  explicit Square(double s) : Rect(s, s) {}
  std::string name() const { return "square"; }
};

struct MyError : std::runtime_error {
  int code;
  MyError(const std::string &m, int c) : std::runtime_error(m), code(c) {}
};

static int depth(int n) {
  if (n == 0) throw MyError("deep", 42);
  return depth(n - 1) + 1;
}

struct Guard {
  std::ostream &os;
  explicit Guard(std::ostream &o) : os(o) {}
  ~Guard() { os << "guard released\n"; }
};

int main() {
  // containers
  std::vector<int> v;
  for (int i = 10; i > 0; --i) v.push_back(i * 3 % 7);
  std::sort(v.begin(), v.end());
  std::cout << "vector:";
  for (size_t i = 0; i < v.size(); ++i) std::cout << ' ' << v[i];
  std::cout << '\n';

  std::map<std::string, int> m;
  const char *words[] = {"pear", "apple", "fig", "apple", "pear", "apple"};
  for (int i = 0; i < 6; ++i) m[words[i]]++;
  for (std::map<std::string, int>::const_iterator it = m.begin(); it != m.end(); ++it)
    std::cout << it->first << '=' << it->second << '\n';

  std::set<int> s(v.begin(), v.end());
  std::list<std::string> l;
  l.push_back("b"); l.push_front("a"); l.push_back("c");
  std::cout << "set size " << s.size() << ", list";
  for (std::list<std::string>::iterator it = l.begin(); it != l.end(); ++it)
    std::cout << ' ' << *it;
  std::cout << '\n';

  // streams
  std::ostringstream os;
  os << "pi=" << 3.25 << " hex=" << std::hex << 255 << std::dec << " n=" << -17;
  std::cout << os.str() << '\n';
  std::istringstream is("12 34 abc");
  int a = 0, b = 0;
  std::string w;
  is >> a >> b >> w;
  std::cout << "parsed " << a + b << ' ' << w << '\n';

  // virtual functions
  std::vector<Shape *> shapes;
  shapes.push_back(new Rect(2, 3));
  shapes.push_back(new Square(4));
  double total = 0;
  for (size_t i = 0; i < shapes.size(); ++i) {
    std::cout << shapes[i]->name() << ' ' << shapes[i]->area() << '\n';
    total += shapes[i]->area();
  }
  for (size_t i = 0; i < shapes.size(); ++i) delete shapes[i];
  std::cout << "total " << total << '\n';

  // exceptions
  try {
    Guard g(std::cout);
    depth(5);
  } catch (const MyError &e) {
    std::cout << "caught " << e.what() << ' ' << e.code << '\n';
  }
  try {
    std::vector<int> small(2);
    small.at(5) = 1;
  } catch (const std::out_of_range &) {
    std::cout << "out_of_range caught\n";
  }
  try {
    throw 7;
  } catch (int x) {
    std::cout << "int " << x << '\n';
  }
  std::cout << "done\n";
  return 0;
}
