#include "polynomial_conv.h"
#include "param_util.h"


/*************************************************************************
Generated by https://people.sc.fsu.edu/~jburkardt/cpp_src/triangle_dunavant_rule/triangle_dunavant_rule.html
Quadrature points of a unit right triangle for degree - 4 polynomials

x                      y                       w

0.10810301816807        0.445948490915965       0.223381589678011
0.445948490915965       0.445948490915965       0.223381589678011
0.445948490915965       0.10810301816807        0.223381589678011
0.816847572980459       0.091576213509771       0.109951743655322
0.091576213509771       0.091576213509771       0.109951743655322
0.091576213509771       0.816847572980459       0.109951743655322
**************************************************************************/
inline void quadrature_degree4(const Geex::vec2& v0, const Geex::vec2& v1, const Geex::vec2&v2,
	std::vector<double>& weights, std::vector<Geex::vec2>& points, std::vector<Geex::vec3>& baryweights)
{
	weights.resize(6); points.resize(6);

	auto triarea2d = [](const Geex::vec2& v0, const Geex::vec2& v1, const Geex::vec2& v2) -> double {
		auto v01 = v1 - v0;
		auto v02 = v2 - v0;
		return 0.5*std::abs(v01[0] * v02[1] - v01[1] * v02[0]);
	};

	const double area = triarea2d(v0, v1, v2);
	weights[0] = weights[1] = weights[2] = 0.223381589678011*area;
	weights[3] = weights[4] = weights[5] = 0.109951743655322*area;

	const double pos_blend[6][2] = {
		{ 0.10810301816807, 0.445948490915965 },
		{ 0.445948490915965, 0.445948490915965 },
		{ 0.445948490915965, 0.10810301816807 },
		{ 0.816847572980459, 0.091576213509771 },
		{ 0.091576213509771, 0.091576213509771 },
		{ 0.091576213509771, 0.816847572980459 }
	};

	for (int i = 0; i < 6; i++)
	{
		points[i] = v0 + pos_blend[i][0] * (v1 - v0) + pos_blend[i][1] * (v2 - v0);
	}

	const Geex::vec3 bary_weights[6] = {
		{ 0.445948490915965, 0.10810301816807, 0.445948490915965 },
		{ 0.10810301816807, 0.445948490915965, 0.445948490915965 },
		{ 0.445948490915965, 0.445948490915965, 0.10810301816807 },
		{ 0.09157621350977004, 0.816847572980459, 0.09157621350977101 },
		{ 0.8168475729804581, 0.09157621350977101, 0.09157621350977101 },
		{ 0.09157621350977004, 0.09157621350977101, 0.816847572980459 }
	};
	baryweights.insert(baryweights.end(), bary_weights, bary_weights + 6);
}


inline double cubic_poly_term(int term, double coord_x, double coord_y) {
	const int xmap[10] = { 0, 0, 1, 0, 1, 2, 0, 1, 2, 3 },
		ymap[10] = { 0, 1, 0, 2, 1, 0, 3, 2, 1, 0 };
	double val = 1.0;
	for (int i = 0; i < xmap[term]; i++) val *= coord_x;
	for (int i = 0; i < ymap[term]; i++) val *= coord_y;
	return val;
}

//map face,vertex -> corner 
inline int fv_corner(Geex::HE_face* face, Geex::HE_vert* vert) {
	auto edge = face->edge;
	int corner = 0;
	do
	{
		if (edge->vert == vert)
			return corner;
		corner++; edge = edge->next;
	} while (edge != face->edge);
	printf("Error: vertex not incident to face!\n");
	return -1;
}

//return list of vertices of a face in order of corners
inline std::vector<Geex::HE_vert*> f_corners(Geex::HE_face* face) {
	std::vector<Geex::HE_vert*> vts;
	auto edge = face->edge;
	do {
		vts.push_back(edge->vert);
		edge = edge->next;
	} while (edge != face->edge);
	return vts;
}

//robust vector normalization
inline Geex::vec2 robust_normalize(const Geex::vec2& v, double threshold = 1.e-10) {
	double len = v.length();
	if (len < threshold)
	{
		return Geex::vec2(0, 0);
	}
	return (1.0 / len)*v;
}


void polynomial_conv(Geex::Mesh3D* mesh, const std::vector<std::vector<Geex::vec3>>& axes, bool use_patch_height, //inputs
	std::vector<COOEntry<float>>& S_fv, std::vector<COOEntry<float>>& S_vf,  //outputs
	std::vector<std::vector<float>>& D_fw, std::vector<std::vector<float>>& D_patchinput,
	std::vector<int> &S_fv_index, std::vector<float> &S_fv_value, std::vector<int> &S_vf_index, std::vector<float> &S_vf_value) {

	const int face_num = mesh->get_num_of_faces();
	const int axis_num = axes[0].size();
	const int fvaq_num = face_num * 3 * axis_num * 6; // face, vertex, axis, quadrature point number
	S_fv.resize(fvaq_num * 3);
	S_fv_index.resize(fvaq_num * 3 * 2);
	S_fv_value.resize(fvaq_num * 3);

	D_fw.resize(fvaq_num, std::vector<float>(10, 0.));
	D_patchinput.resize(fvaq_num, std::vector<float>(use_patch_height ? 4 : 3, 0.));

	std::vector<double> ring_ref_scale(mesh->get_num_of_vertices(), 1.0);
	//S_v,f is the summation of 1-ring faces
	for (int vitr = 0; vitr < mesh->get_num_of_vertices(); vitr++)
	{
		auto tri_area3d = [](Geex::vec3& a, Geex::vec3& b, Geex::vec3& c)->double {
			return Geex::cross(b - a, c - a).length()*0.5;
		};

		Geex::HE_vert* vh = mesh->get_vertex(vitr);
		double ring_area = 0.;
		auto eh = vh->edge;
		do
		{
			if (eh->face)
			{
				auto feh = eh->face->edge;
				ring_area += tri_area3d(feh->vert->pos, feh->next->vert->pos, feh->prev->vert->pos);
			}

			eh = eh->pair->next;
		} while (eh != vh->edge);
		if (!is_valid(ring_area) || ring_area < 1e-10)
		{
			continue;
		}
		ring_ref_scale[vitr] = std::sqrt(ring_area);

		for (int axis = 0; axis < axis_num; axis++)
		{
			auto eh = vh->edge;
			do
			{
				if (eh->face)
				{
					Geex::HE_face* face = eh->face;
					int corner = fv_corner(face, vh);
					COOEntry<float> entry(vh->id*axis_num + axis, (face->id * 3 + corner)*axis_num + axis, 1.0);
					S_vf.push_back(entry);
					S_vf_index.push_back(entry.row());
					S_vf_index.push_back(entry.col());
					S_vf_value.push_back(entry.val());
				}
				eh = eh->pair->next;
			} while (eh != vh->edge);
		}
	}


#pragma omp parallel for
	for (int face = 0; face < face_num; face++)
	{
		Geex::HE_face* fh = mesh->get_face(face);
		std::vector<Geex::HE_vert*> vts = f_corners(fh);
		Geex::vec3 pts[3] = { vts[0]->pos, vts[1]->pos, vts[2]->pos };

		for (int corner = 0; corner < 3; corner++)
		{
			Geex::HE_vert* fv = vts[corner]; Geex::vec3 fv_pos = pts[corner];
			Geex::vec3 edge01 = pts[(corner + 1) % 3] - fv_pos,
				edge02 = pts[(corner + 2) % 3] - fv_pos;

			const int axis_offset[3] = { 0,
				get_axis_map_p2p(fv->normal, vts[(corner + 1) % 3]->normal, axis_num, axes[fv->id], axes[vts[(corner + 1) % 3]->id]),
				get_axis_map_p2p(fv->normal, vts[(corner + 2) % 3]->normal, axis_num, axes[fv->id], axes[vts[(corner + 2) % 3]->id]) };

			for (int axis = 0; axis < axis_num; axis++)
			{
				Geex::vec3 frame[2];
				frame[0] = axes[fv->id][axis]; frame[1] = Geex::cross(fv->normal, frame[0]);

				Geex::vec2 pts_2d[3] = { Geex::vec2(0,0),
					robust_normalize(Geex::vec2(dot(edge01, frame[0]), dot(edge01, frame[1]))) * edge01.length() / ring_ref_scale[fv->id],
					robust_normalize(Geex::vec2(dot(edge02, frame[0]), dot(edge02, frame[1]))) * edge02.length() / ring_ref_scale[fv->id] };

				std::vector<double> qua_weights;
				std::vector<Geex::vec2> qua_pts;
				std::vector<Geex::vec3> qua_bary_weights;
				quadrature_degree4(pts_2d[0], pts_2d[1], pts_2d[2], qua_weights, qua_pts, qua_bary_weights);

				for (int qua_itr = 0; qua_itr < 6; qua_itr++)
				{
					const int offset = ((face * 3 + corner) * axis_num + axis) * 6 + qua_itr;

					for (int i = 0; i < 3; i++)
					{
						S_fv[offset * 3 + i].row() = offset; //F*3*A*6
						S_fv[offset * 3 + i].col() = vts[(corner + i) % 3]->id*axis_num + (axis + axis_offset[i]) % axis_num; //V*A
						S_fv[offset * 3 + i].val() = qua_bary_weights[qua_itr][i];
						S_fv_index[(offset * 3 + i) * 2 + 0] = S_fv[offset * 3 + i].row();
						S_fv_index[(offset * 3 + i) * 2 + 1] = S_fv[offset * 3 + i].col();
						S_fv_value[offset * 3 + i] = S_fv[offset * 3 + i].val();
					}

					for (int i = 0; i < 10; i++)
					{
						D_fw[offset][i] = cubic_poly_term(i, qua_pts[qua_itr][0], qua_pts[qua_itr][1])*qua_weights[qua_itr]; //quadature weights are multiplied to D_f,w.
						if (D_fw[offset][i] > 1e+02)
						{
							std::cout << face << " " << i << " " << qua_weights[qua_itr] << " " << cubic_poly_term(i, qua_pts[qua_itr][0], qua_pts[qua_itr][1]) << std::endl;
							std::cout << qua_pts[qua_itr][0] << " " << qua_pts[qua_itr][1] << std::endl;
						}
					}
				}

				// local patch input signals
				Geex::vec3 local_nmls[3]; double local_hgts[3];
				for (int i = 0; i < 3; i++)
				{
					local_nmls[i][0] = dot(vts[(corner + i) % 3]->normal, fv->normal);
					local_nmls[i][1] = dot(vts[(corner + i) % 3]->normal, frame[0]);
					local_nmls[i][2] = dot(vts[(corner + i) % 3]->normal, frame[1]);
					local_hgts[i] = dot(vts[(corner + i) % 3]->pos - fv->pos, fv->normal) / ring_ref_scale[fv->id];
				}
				for (int qua_itr = 0; qua_itr < 6; qua_itr++)
				{
					const int offset = ((face * 3 + corner) * axis_num + axis) * 6 + qua_itr;

					Geex::vec3 qua_nml(0, 0, 0); double qua_hgt = 0.0;
					for (int i = 0; i < 3; i++)
					{
						qua_nml += qua_bary_weights[qua_itr][i] * local_nmls[i];
						qua_hgt += qua_bary_weights[qua_itr][i] * local_hgts[i];
					}
					D_patchinput[offset][0] = qua_nml[0]; D_patchinput[offset][1] = qua_nml[1]; D_patchinput[offset][2] = qua_nml[2];
					if (use_patch_height)
					{
						D_patchinput[offset][3] = qua_hgt;
					}
				}
			}
		}
	}
}


void PolynomialCovVis::build_polynomialconv(double ref_scale) {
	polynomial_conv(mesh, axes, false, S_fv, S_vf, D_fw, D_patchinput, S_fv_index, S_fv_value, S_vf_index, S_vf_value);
}


void PolynomialCovVis::draw(int axis) {
	if (mesh == NULL || marked_pt<0 || marked_pt >= mesh->get_num_of_vertices())
	{
		return;
	}

	const int anum = axes[0].size();

	Geex::HE_vert* vh = mesh->get_vertex(marked_pt);
	Geex::HE_edge* eh = vh->edge;

	double svf_val = -1.;
	for (auto& svf : S_vf)
	{
		if (svf.row() / anum == vh->id)
		{
			svf_val = svf.val(); break;
		}
	}
	if (svf_val < 0) {
		printf("Warning: vertex not found in S_vf!\n");
	}

	glDisable(GL_LIGHTING);
	glPointSize(5.);
	glBegin(GL_POINTS);
	do
	{
		if (eh->face)
		{
			Geex::HE_face* fh = eh->face;
			const int corner = fv_corner(fh, vh);

			for (int qua_itr = 0; qua_itr < 6; qua_itr++)
			{
				const int offset = ((fh->id * 3 + corner)*anum + axis) * 6 + qua_itr;
				Geex::vec3 qua_pt(0, 0, 0);
				for (int i = 0; i < 3; i++)
				{
					auto& entry = S_fv[offset * 3 + i];
					int vidx = entry.col() / anum;
					qua_pt += entry.val()*mesh->get_vertex(vidx)->pos;
				}
				glColor3f(0.2, 0.2, 0.2);
				glVertex(qua_pt);

				double poly_val = 0.;
				const double sampling_coeff[10] = { 0,1,0,-1,0,0,-1,0,0,0 };
				for (int i = 0; i < 10; i++)
				{
					poly_val += sampling_coeff[i] * D_fw[offset][i];
				}
				poly_val *= svf_val;
				glColor3f(0.2, 0.7, 0.5);
				glVertex(qua_pt + poly_val*fh->normal);
			}
		}
		eh = eh->pair->next;
	} while (eh != vh->edge);
	glEnd();
	glEnable(GL_LIGHTING);
}