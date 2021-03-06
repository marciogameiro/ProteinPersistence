/// pers_weighted_alpha_3d.cpp
/// Author: Emerson Escolar, Marcio Gameiro, Shaun Harker
/// 2017 MIT LICENSE

#include <fstream>
#include <cassert>
#include <sstream>
#include <list>
#include <map>

#include <phat/boundary_matrix.h>
#include <phat/compute_persistence_pairs.h>
// main data structure (choice affects performance)
#include <phat/representations/vector_vector.h>
#include <phat/algorithms/twist_reduction.h>


typedef phat::column Column;

typedef phat::boundary_matrix< phat::vector_vector > Bdd_Type;
typedef std::pair<int, double> DimAlpha_Type;
typedef std::vector<DimAlpha_Type> AlphaMap_Type;

typedef int Dim;
typedef int Index;

typedef std::vector<Dim> Dim_container;
typedef std::vector<std::vector<Index> > Matrix;

/// <https://doc.cgal.org/latest/Alpha_shapes_3/index.html#Alpha_shapes_3AlphaShape3OrFixedAlphaShape3>
/// https://doc.cgal.org/latest/Alpha_shapes_3/index.html
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Regular_triangulation_3.h>
#include <CGAL/Alpha_shape_cell_base_3.h>
#include <CGAL/Alpha_shape_vertex_base_3.h>
#include <CGAL/Alpha_shape_3.h>
#include <CGAL/Object.h>

typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef CGAL::Regular_triangulation_vertex_base_3<K>        Rvb;
typedef CGAL::Alpha_shape_vertex_base_3<K,Rvb>              Vb;
typedef CGAL::Regular_triangulation_cell_base_3<K>          Rcb;
typedef CGAL::Alpha_shape_cell_base_3<K,Rcb>                Cb;
typedef CGAL::Triangulation_data_structure_3<Vb,Cb>         Tds;
typedef CGAL::Regular_triangulation_3<K,Tds>                Triangulation_3;
typedef CGAL::Alpha_shape_3<Triangulation_3>                Alpha_shape_3;
typedef Alpha_shape_3::Cell_handle                          Cell_handle;
typedef Alpha_shape_3::Vertex_handle                        Vertex_handle;
typedef Alpha_shape_3::Facet                                Facet;
typedef Alpha_shape_3::Edge                                 Edge;
typedef Triangulation_3::Weighted_point                     Weighted_point;
typedef Triangulation_3::Bare_point                         Bare_point;
///

typedef Triangulation_3::Cell_circulator          Cell_circulator;
typedef Triangulation_3::Finite_cells_iterator    Finite_cells_iterator;
typedef Triangulation_3::Finite_facets_iterator   Finite_facets_iterator;
typedef Triangulation_3::Finite_edges_iterator    Finite_edges_iterator;
typedef Triangulation_3::Finite_vertices_iterator Finite_vertices_iterator;

typedef Alpha_shape_3::FT FT;

typedef CGAL::Alpha_status<Alpha_shape_3::FT>   Alpha_status;
typedef CGAL::Compact_container<Alpha_status>   Alpha_status_container;
typedef Alpha_status_container::iterator        Alpha_status_iterator;
typedef std::pair<Vertex_handle, Vertex_handle> Vertex_handle_pair;

template<typename Index_>
struct Vertex_info_3
{
  typedef Index_ Index;
  Vertex_info_3() {
    index_=boost::none;
  }

  bool has_index() {
    return bool(index_);
  }

  Index index() {
    CGAL_assertion(has_index());
    return index_.get();
  }

  void set_index(Index I) {
    index_=I;
  }

private: 
  boost::optional<Index> index_;
};

template<typename Index_>
struct Cell_info_3
{
  typedef Index_ Index;

  Cell_info_3() {
    for(std::size_t i=0; i<6; i++) {
      edge_index_[i] = boost::none;
    }

    for(std::size_t i=0; i<4; i++) {
      facet_index_[i] = boost::none;
    }
  }

  int edge_conv(int i, int j) {
    if(i>j) std::swap(i,j);
    if(i==0 && j==1) return 0;
    if(i==0 && j==2) return 1;
    if(i==0 && j==3) return 2;
    if(i==1 && j==2) return 3;
    if(i==1 && j==3) return 4;
    if(i==2 && j==3) return 5;
    //should'nt reach here!
    throw;
    return -1;
  }

  bool has_edge_index(int i, int j) {
    return bool(edge_index_[edge_conv(i,j)]);
  }

  Index edge_index(int i, int j) {
    CGAL_assertion(has_edge_index(i,j));
    int k = edge_conv(i,j);
    return edge_index_[k].get();
  }

  bool has_facet_index(int i) {
    CGAL_assertion(i>=0 && i<4);
    return bool(facet_index_[i]);
  }

  Index facet_index(int i) {
    CGAL_assertion(has_facet_index(i));
    return facet_index_[i].get();
  }

  void set_edge_index(int i, int j, Index I) {
    edge_index_[edge_conv(i,j)]=I;
  }

  void set_facet_index(int i, Index I) {
    facet_index_[i]=I;
  }
private:
  boost::optional<Index> edge_index_[6];
  boost::optional<Index> facet_index_[4];
};

void set_index_of_edge(const Triangulation_3& T, const Edge& e, std::map<Cell_handle,
                       Cell_info_3<Index> >& c_info, Index I)
{
  Vertex_handle v1 = e.first->vertex(e.second);
  Vertex_handle v2 = e.first->vertex(e.third);

  Cell_circulator ch=T.incident_cells(e);
  Cell_circulator ch_start=ch;
  int count=0;
  do {
    c_info[(Cell_handle)ch].set_edge_index(ch->index(v1), ch->index(v2), I);
    ch++;
    count++;
  } while(ch!=ch_start);
}

void set_index_of_facet(const Triangulation_3& T, const Facet& f,
                        std::map<Cell_handle, Cell_info_3<Index> >& c_info, Index I)
{
  c_info[f.first].set_facet_index(f.second, I);
  Facet mf = T.mirror_facet(f);    
  c_info[mf.first].set_facet_index(mf.second, I); 
}

template<typename Triple>
struct Sort_triples {
  bool operator() (const Triple& a, const Triple& b) {
    if(a.first < b.first) return true;
    if(a.first > b.first) return false;
    return a.second < b.second;
  }
};


typedef std::vector<std::vector<int>> filtered_complex_t;
typedef std::vector<std::pair<double,double>> persistence_diagram_t;
typedef std::vector<persistence_diagram_t> persistence_diagrams_t;

//std::pair<filtered_complex_t, persistence_diagrams_t>
persistence_diagrams_t
pdb2persistence(std::vector<std::vector<double>> const& data) {

  // Read data into list structure

  std::list<Weighted_point> lp;
  for ( auto const& atom : data ) {
    // atom == {x,y,z,r}
    lp . push_back( Weighted_point(Bare_point(atom[0],atom[1],atom[2]), atom[3]*atom[3]) );
  }

  // Create alpha shape

  Alpha_shape_3 shape(lp.begin(),lp.end(), 0, Alpha_shape_3::GENERAL);

  //
  // Obtain circumradii for each cell in alpha complex
  //

  typedef CGAL::Triple<FT,int,CGAL::Object> Triple;
  std::vector<Triple> circumradii;
  FT alpha_value;

  // Obtain circumradii of 0-cells

  for (Finite_vertices_iterator vertex = shape.finite_vertices_begin();
       vertex != shape.finite_vertices_end(); vertex++) {
    Vertex_handle vh(vertex);

    Alpha_status* as = vh->get_alpha_status();

    if(as->is_Gabriel()){
      alpha_value = as->alpha_min();
      //std::cout << "dim 0. gabriel. " << alpha_value << "\n";
    }
    else {
      alpha_value = as->alpha_mid();
      //std::cout << "dim 0. not gabriel. " << alpha_value << "\n";
    }

    circumradii.push_back(CGAL::make_triple (alpha_value, 0, CGAL::make_object(vh)));
  }

  // Obtain circumradii of 1-cells

  for (Finite_edges_iterator edge = shape.finite_edges_begin();
       edge != shape.finite_edges_end(); edge++) {
    Vertex_handle v1 = edge->first->vertex(edge->second);
    Vertex_handle v2 = edge->first->vertex(edge->third);

    Vertex_handle_pair vhp = (v1 < v2 ? std::make_pair(v1,v2) : std::make_pair(v2,v1));

    Alpha_status_iterator as = shape.get_edge_alpha_map()->find(vhp)->second;

    if(as->is_Gabriel()){
      alpha_value = as->alpha_min();
      //std::cout << "dim 1. gabriel. " << alpha_value << "\n";
    }
    else {
      alpha_value = as->alpha_mid();
      //std::cout << "dim 1. not gabriel. " << alpha_value << "\n";
    }

    circumradii.push_back(CGAL::make_triple (alpha_value, 1, CGAL::make_object(*edge)));
  }

  // Obtain circumradii of 2-cells

  for (Finite_facets_iterator f = shape.finite_facets_begin();
       f != shape.finite_facets_end(); f++) {
    Vertex_handle v1 = f->first->vertex((f->second+1)%4);
    Vertex_handle v2 = f->first->vertex((f->second+2)%4);
    Vertex_handle v3 = f->first->vertex((f->second+3)%4);

    Alpha_status_iterator as = f->first->get_facet_status(f->second);

    if(as->is_Gabriel()){
      alpha_value = as->alpha_min();
      //std::cout << "dim 2. gabriel. " << alpha_value << "\n";
    }
    else {
      alpha_value = as->alpha_mid();
      //std::cout << "dim 2. not gabriel. " << alpha_value << "\n";
    }
    circumradii.push_back(CGAL::make_triple (alpha_value, 2, CGAL::make_object(*f)));
  }

  // Obtain circumradii of 3-cells

  for (Finite_cells_iterator cit = shape.finite_cells_begin();
       cit != shape.finite_cells_end(); cit++) {
    Cell_handle ch(cit);
    Vertex_handle v1 = cit->vertex(0);
    Vertex_handle v2 = cit->vertex(1);
    Vertex_handle v3 = cit->vertex(2);
    Vertex_handle v4 = cit->vertex(3);

    //std::cout << "dim 3. gabriel. " << cit->get_alpha() << "\n";

    circumradii.push_back(CGAL::make_triple (cit->get_alpha(), 3, CGAL::make_object(ch)));
  }

  // Create filtration by sorting circumradii of each cell

  //std::cerr << "Sorting alpha_values..." << std::endl;

  std::sort(circumradii.begin(),circumradii.end(), Sort_triples<Triple>());

  //std::cerr << "Filtration of size " << circumradii.size() << std::endl;

  Vertex_handle v;
  Edge e;
  Facet f;
  Cell_handle c;

  std::size_t filtration_index = 0;
  std::size_t filtration_size = circumradii.size();

  Bdd_Type boundary_matrix;
  boundary_matrix.set_num_cols( filtration_size );

  AlphaMap_Type alpha_map(filtration_size);

  Index curr_index = 0;

  std::map<Vertex_handle, Vertex_info_3<Index> > v_info;
  std::map<Cell_handle, Cell_info_3<Index> > c_info;

  for(std::vector<Triple>::const_iterator it = circumradii.begin();
      it != circumradii.end(); it++) {
    // if(filtration_index % 5000 == 0) {
    //   std::cerr << filtration_index << " of " << filtration_size << std::endl;
    // }

    filtration_index++;

    const CGAL::Object& obj = it->third;
    Column col;

    // alpha_map[curr_index] = std::make_pair (it->second, it->first.exact().to_double() ); // 2017-10-18 -SH
    alpha_map[curr_index] = std::make_pair (it->second, it->first );  // 2017-10-18 -SH

    if(CGAL::assign(v,obj)) {
      // std::cout << "Vertex " << it->second << std::endl;
      boundary_matrix.set_dim(curr_index, 0);
      v_info[v].set_index(curr_index);
      boundary_matrix.set_col(curr_index, col);
    }

    if(CGAL::assign(e,obj)) {
      // std::cout << "Edge " << it->second << std::endl;
      boundary_matrix.set_dim(curr_index, 1);
      Vertex_handle v1 = e.first->vertex(e.second);
      CGAL_assertion(v_info.at(v1).has_index());
      Vertex_handle v2 = e.first->vertex(e.third);
      CGAL_assertion(v_info.at(v2).has_index());

      Index i1 = v_info[v1].index();
      Index i2 = v_info[v2].index();

      if (i1 > i2) {
        std::swap(v1,v2);
        std::swap(i1,i2);
      }

      col.push_back(i1);
      col.push_back(i2);
      boundary_matrix.set_col(curr_index, col);

      set_index_of_edge(shape, e, c_info, curr_index);
    }

    if(CGAL::assign(f,obj)) {
      // std::cout << "Facet " << it->second << std::endl;
      boundary_matrix.set_dim(curr_index, 2);

      Index i1= c_info.at(f.first).edge_index((f.second+1)%4, (f.second+2)%4 );
      col.push_back(i1);

      Index i2= c_info.at(f.first).edge_index((f.second+1)%4, (f.second+3)%4 );
      col.push_back(i2);

      Index i3= c_info.at(f.first).edge_index((f.second+2)%4, (f.second+3)%4 );
      col.push_back(i3);

      std::sort(col.begin(),col.end());
      boundary_matrix.set_col(curr_index, col);

      set_index_of_facet(shape, f, c_info, curr_index);
    }

    if(CGAL::assign(c,obj)) {
      // std::cout << "Cell " << it->second << std::endl;
      boundary_matrix.set_dim(curr_index, 3);

      col.push_back(c_info.at(c).facet_index(0));
      col.push_back(c_info.at(c).facet_index(1));
      col.push_back(c_info.at(c).facet_index(2));
      col.push_back(c_info.at(c).facet_index(3));
      std::sort(col.begin(),col.end());
      boundary_matrix.set_col(curr_index, col);
    }

    curr_index++;
  }

  //filtered_complex_t filtered_complex;


  // boundary_matrix.save_binary("alpha_filtration.bin");
  //boundary_matrix.save_ascii("alpha_filtration.txt");


  // Compute persistence using PHAT
  phat::persistence_pairs pairs;
  phat::compute_persistence_pairs< phat::twist_reduction > (pairs, boundary_matrix);
  pairs.sort();


  // Store persistence calculation in desired data structure
  persistence_diagrams_t persistence_diagrams(3);
  for( phat::index idx = 0; idx < pairs.get_num_pairs(); idx++ ) {
    Index b_i = pairs.get_pair( idx ).first;
    Index d_i = pairs.get_pair( idx ).second;
    int cur_dim = circumradii[b_i].second;
    FT birth = circumradii[b_i].first;
    FT death = circumradii[d_i].first;
    if (birth != death) persistence_diagrams[cur_dim].push_back({birth,death}); 
  }
  //return {filtered_complex, persistence_diagrams};
  return persistence_diagrams;
}

/// Python Bindings

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;

void pdb2persistenceModule(py::module &m) {
  m.def("pdb2persistence", &pdb2persistence, R"pbdoc(
        Given list of lists [x,y,z,r] of atomic center coordinates and atomic radii,
        builds a weightd alpha complex and computes the 0, 1, and 2 dimensional 
        persistence diagrams, returned as a list of lists of 2-tuples of birth/death times.
    )pbdoc");
}

PYBIND11_MODULE(ProteinPersistence, m) {
    m.doc() = R"pbdoc(
        ProteinPersistence)pbdoc";
    //     -----------------------

    //     .. currentmodule:: pdb2persistence

    //     .. autosummary::
    //        :toctree: _generate

    //        pdb2persistence
    // )pbdoc";

  pdb2persistenceModule(m);

#ifdef VERSION_INFO
    m.attr("__version__") = VERSION_INFO;
#else
    m.attr("__version__") = "dev";
#endif
}

