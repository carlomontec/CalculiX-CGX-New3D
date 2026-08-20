/* --------------------------------------------------------------------  */
/*                          CALCULIX                                     */
/*                   - GRAPHICAL INTERFACE -                             */
/*                                                                       */
/*     A 3-dimensional pre- and post-processor for finite elements       */
/*              Copyright (C) 1996 Klaus Wittig                          */
/*                                                                       */
/*     This program is free software; you can redistribute it and/or     */
/*     modify it under the terms of the GNU General Public License as    */
/*     published by the Free Software Foundation; version 2 of           */
/*     the License.                                                      */
/* --------------------------------------------------------------------  */

/* write2vtu.c - Export mesh and field datasets to VTK XML Unstructured Grid (.vtu) */
/* and ParaView Data collection (.pvd)                                               */

#include <cgx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern int cur_lc;
int readfrdblock( int lc, Summen *anz, Nodes *node, Datasets *lcase );
void calcDatasets( int num_olc, Summen *anz, Nodes *node, Datasets *lcase );

/* Node permutation map from CGX element node ordering to VTK standard ordering */
static const int vtk_map_hex20[20] = {
  0, 1, 2, 3, 4, 5, 6, 7,        /* Corners: 0-7 */
  8, 9, 10, 11,                  /* Bottom edges: 0-1, 1-2, 2-3, 3-0 */
  16, 17, 18, 19,                /* Top edges in VTK (CGX 16..19) */
  12, 13, 14, 15                 /* Vertical edges in VTK (CGX 12..15) */
};

static const int vtk_map_penta15[15] = {
  0, 1, 2, 3, 4, 5,              /* Corners: bottom 0-2, top 3-5 */
  6, 7, 8,                       /* Bottom edges */
  12, 13, 14,                    /* Top edges in VTK (CGX 12..14) */
  9, 10, 11                      /* Vertical edges in VTK (CGX 9..11) */
};

/* Calculate Principal Stresses P1 >= P2 >= P3 and von Mises stress analytically */
static void calc_principal_and_mises(double sxx, double syy, double szz,
                                     double sxy, double syz, double szx,
                                     double *p1, double *p2, double *p3, double *vm)
{
  double p, s11, s22, s33, J2, J3, q, arg, phi;
  double r1, r2, r3, tmp;

  /* Mean hydrostatic stress */
  p = (sxx + syy + szz) / 3.0;

  /* Deviatoric stress components */
  s11 = sxx - p;
  s22 = syy - p;
  s33 = szz - p;

  /* Second invariant J2 */
  J2 = 0.5 * (s11 * s11 + s22 * s22 + s33 * s33) + sxy * sxy + syz * syz + szx * szx;

  /* Von Mises equivalent stress: sqrt(3 * J2) */
  if (J2 > 0.0) *vm = sqrt(3.0 * J2);
  else *vm = 0.0;

  /* Determinant of deviatoric stress (Third invariant J3) */
  J3 = s11 * (s22 * s33 - syz * syz)
     - sxy * (sxy * s33 - syz * szx)
     + szx * (sxy * syz - s22 * szx);

  if (J2 < 1e-20)
  {
    *p1 = p;
    *p2 = p;
    *p3 = p;
    return;
  }

  q = 2.0 * sqrt(J2 / 3.0);
  arg = (3.0 * J3) / (q * J2);
  if (arg > 1.0) arg = 1.0;
  if (arg < -1.0) arg = -1.0;

  phi = acos(arg) / 3.0;

  r1 = p + q * cos(phi);
  r2 = p + q * cos(phi + (2.0 * M_PI / 3.0));
  r3 = p + q * cos(phi + (4.0 * M_PI / 3.0));

  /* Sort descending: p1 >= p2 >= p3 */
  if (r1 < r2) { tmp = r1; r1 = r2; r2 = tmp; }
  if (r2 < r3) { tmp = r2; r2 = r3; r3 = tmp; }
  if (r1 < r2) { tmp = r1; r1 = r2; r2 = tmp; }

  *p1 = r1;
  *p2 = r2;
  *p3 = r3;
}

/* Map CGX element types to VTK cell types */
static int get_vtk_cell_type(int cgx_type, int *num_nodes)
{
  switch (cgx_type)
  {
    case 1:  *num_nodes = 8;  return 12; /* VTK_HEXAHEDRON */
    case 2:  *num_nodes = 6;  return 13; /* VTK_WEDGE */
    case 3:  *num_nodes = 4;  return 10; /* VTK_TETRA */
    case 4:  *num_nodes = 20; return 25; /* VTK_QUADRATIC_HEXAHEDRON */
    case 5:  *num_nodes = 15; return 26; /* VTK_QUADRATIC_WEDGE */
    case 6:  *num_nodes = 10; return 24; /* VTK_QUADRATIC_TETRA */
    case 7:  *num_nodes = 3;  return 5;  /* VTK_TRIANGLE */
    case 8:  *num_nodes = 6;  return 22; /* VTK_QUADRATIC_TRIANGLE */
    case 9:  *num_nodes = 4;  return 9;  /* VTK_QUAD */
    case 10: *num_nodes = 8;  return 23; /* VTK_QUADRATIC_QUAD */
    case 11: *num_nodes = 2;  return 3;  /* VTK_LINE */
    case 12: *num_nodes = 3;  return 21; /* VTK_QUADRATIC_EDGE */
    default: *num_nodes = 0;  return 0;
  }
}

/* Write a single .vtu file for a given physical step */
static int write_single_vtu_file(const char *filename,
                                 int num_step_lcs, int *step_lc_indices,
                                 double step_val, int step_num, int analysis_type,
                                 Summen *anz, Nodes *node, Elements *elem,
                                 Sets *set, int setNr, Datasets *lcase,
                                 int num_pts, int *pts_nodenr, int *nodeMap,
                                 int num_elems, int *elem_indices)
{
  FILE *fp;
  int i, j, k, lc, eid, nid, vtk_type, n_nodes;
  long offset = 0;
  double sxx, syy, szz, sxy, syz, szx;
  double p1, p2, p3, vm;

  fp = fopen(filename, "w");
  if (!fp)
  {
    printf("ERROR: Could not open output file: %s\n", filename);
    return 0;
  }

  fprintf(fp, "<?xml version=\"1.0\"?>\n");
  fprintf(fp, "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n");
  fprintf(fp, "  <UnstructuredGrid>\n");

  /* Global / Step Metadata (FieldData placed directly under UnstructuredGrid) */
  fprintf(fp, "    <FieldData>\n");
  if (analysis_type >= 2)
  {
    /* Modal / Frequency Analysis */
    fprintf(fp, "      <DataArray type=\"Float64\" Name=\"Frequency_Hz\" NumberOfTuples=\"1\" format=\"ascii\">\n");
    fprintf(fp, "        %.6f\n", step_val);
    fprintf(fp, "      </DataArray>\n");
    fprintf(fp, "      <DataArray type=\"Int32\" Name=\"Mode_Number\" NumberOfTuples=\"1\" format=\"ascii\">\n");
    fprintf(fp, "        %d\n", step_num);
    fprintf(fp, "      </DataArray>\n");
    fprintf(fp, "      <DataArray type=\"Float64\" Name=\"Angular_Frequency_rad_s\" NumberOfTuples=\"1\" format=\"ascii\">\n");
    fprintf(fp, "        %.6f\n", 2.0 * M_PI * step_val);
    fprintf(fp, "      </DataArray>\n");
  }
  else
  {
    /* Static / Transient Analysis */
    fprintf(fp, "      <DataArray type=\"Float64\" Name=\"Time\" NumberOfTuples=\"1\" format=\"ascii\">\n");
    fprintf(fp, "        %.6f\n", step_val);
    fprintf(fp, "      </DataArray>\n");
    fprintf(fp, "      <DataArray type=\"Int32\" Name=\"Step_Number\" NumberOfTuples=\"1\" format=\"ascii\">\n");
    fprintf(fp, "        %d\n", step_num);
    fprintf(fp, "      </DataArray>\n");
  }
  fprintf(fp, "    </FieldData>\n");

  fprintf(fp, "    <Piece NumberOfPoints=\"%d\" NumberOfCells=\"%d\">\n", num_pts, num_elems);

  /* 1. Point Coordinates */
  fprintf(fp, "      <Points>\n");
  fprintf(fp, "        <DataArray type=\"Float64\" Name=\"Points\" NumberOfComponents=\"3\" format=\"ascii\">\n");
  for (i = 0; i < num_pts; i++)
  {
    nid = pts_nodenr[i];
    fprintf(fp, "          %.9e %.9e %.9e\n", node[nid].nx, node[nid].ny, node[nid].nz);
  }
  fprintf(fp, "        </DataArray>\n");
  fprintf(fp, "      </Points>\n");

  /* 2. Cells (Topology, Offsets, Types) */
  fprintf(fp, "      <Cells>\n");

  /* Connectivity */
  fprintf(fp, "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n");
  for (i = 0; i < num_elems; i++)
  {
    eid = elem_indices[i];
    vtk_type = get_vtk_cell_type(elem[eid].type, &n_nodes);
    fprintf(fp, "         ");
    for (j = 0; j < n_nodes; j++)
    {
      int map_idx = j;
      if (elem[eid].type == 4) map_idx = vtk_map_hex20[j];
      else if (elem[eid].type == 5) map_idx = vtk_map_penta15[j];

      nid = elem[eid].nod[map_idx];
      fprintf(fp, " %d", (nid >= 0 && nid <= anz->nmax) ? nodeMap[nid] : 0);
    }
    fprintf(fp, "\n");
  }
  fprintf(fp, "        </DataArray>\n");

  /* Offsets */
  fprintf(fp, "        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n");
  offset = 0;
  for (i = 0; i < num_elems; i++)
  {
    eid = elem_indices[i];
    get_vtk_cell_type(elem[eid].type, &n_nodes);
    offset += n_nodes;
    fprintf(fp, "          %ld\n", offset);
  }
  fprintf(fp, "        </DataArray>\n");

  /* Types */
  fprintf(fp, "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n");
  for (i = 0; i < num_elems; i++)
  {
    eid = elem_indices[i];
    vtk_type = get_vtk_cell_type(elem[eid].type, &n_nodes);
    fprintf(fp, "          %d\n", vtk_type);
  }
  fprintf(fp, "        </DataArray>\n");
  fprintf(fp, "      </Cells>\n");

  /* 3. Point Data Fields */
  fprintf(fp, "      <PointData>\n");
  if (anz->l > 0 && lcase != NULL && num_step_lcs > 0)
  {
    for (k = 0; k < num_step_lcs; k++)
    {
      lc = step_lc_indices[k];
      if (lc < 0 || lc >= anz->l) continue;

      /* Ensure dataset is loaded in memory */
      if (!lcase[lc].loaded)
      {
        readfrdblock(lc, anz, node, lcase);
        calcDatasets(lc, anz, node, lcase);
      }

      if (!lcase[lc].loaded || lcase[lc].dat == NULL) continue;

      /* Displacements Vector (3 components) */
      if (compare(lcase[lc].name, "DISP", 4) == 4 || (lcase[lc].ncomps >= 3 && compare(lcase[lc].compName[0], "D1", 2) == 2))
      {
        fprintf(fp, "        <DataArray type=\"Float32\" Name=\"Displacements\" NumberOfComponents=\"3\" format=\"ascii\">\n");
        for (i = 0; i < num_pts; i++)
        {
          nid = pts_nodenr[i];
          fprintf(fp, "          %.6e %.6e %.6e\n",
                  lcase[lc].dat[0][nid], lcase[lc].dat[1][nid], lcase[lc].dat[2][nid]);
        }
        fprintf(fp, "        </DataArray>\n");
      }
      /* Stress Tensor & Precomputed Invariants */
      else if (compare(lcase[lc].name, "STRESS", 6) == 6 || lcase[lc].ncomps >= 6)
      {
        /* 6-component Stress Tensor */
        fprintf(fp, "        <DataArray type=\"Float32\" Name=\"Stress\" NumberOfComponents=\"6\" format=\"ascii\">\n");
        for (i = 0; i < num_pts; i++)
        {
          nid = pts_nodenr[i];
          fprintf(fp, "          %.6e %.6e %.6e %.6e %.6e %.6e\n",
                  lcase[lc].dat[0][nid], lcase[lc].dat[1][nid], lcase[lc].dat[2][nid],
                  lcase[lc].dat[3][nid], lcase[lc].dat[4][nid], lcase[lc].dat[5][nid]);
        }
        fprintf(fp, "        </DataArray>\n");

        /* Precomputed von Mises Scalar */
        fprintf(fp, "        <DataArray type=\"Float32\" Name=\"vonMises\" NumberOfComponents=\"1\" format=\"ascii\">\n");
        for (i = 0; i < num_pts; i++)
        {
          nid = pts_nodenr[i];
          sxx = lcase[lc].dat[0][nid]; syy = lcase[lc].dat[1][nid]; szz = lcase[lc].dat[2][nid];
          sxy = lcase[lc].dat[3][nid]; syz = lcase[lc].dat[4][nid]; szx = lcase[lc].dat[5][nid];
          calc_principal_and_mises(sxx, syy, szz, sxy, syz, szx, &p1, &p2, &p3, &vm);
          fprintf(fp, "          %.6e\n", vm);
        }
        fprintf(fp, "        </DataArray>\n");

        /* Principal Stresses */
        fprintf(fp, "        <DataArray type=\"Float32\" Name=\"PrincipalStress_1\" NumberOfComponents=\"1\" format=\"ascii\">\n");
        for (i = 0; i < num_pts; i++)
        {
          nid = pts_nodenr[i];
          sxx = lcase[lc].dat[0][nid]; syy = lcase[lc].dat[1][nid]; szz = lcase[lc].dat[2][nid];
          sxy = lcase[lc].dat[3][nid]; syz = lcase[lc].dat[4][nid]; szx = lcase[lc].dat[5][nid];
          calc_principal_and_mises(sxx, syy, szz, sxy, syz, szx, &p1, &p2, &p3, &vm);
          fprintf(fp, "          %.6e\n", p1);
        }
        fprintf(fp, "        </DataArray>\n");

        fprintf(fp, "        <DataArray type=\"Float32\" Name=\"PrincipalStress_2\" NumberOfComponents=\"1\" format=\"ascii\">\n");
        for (i = 0; i < num_pts; i++)
        {
          nid = pts_nodenr[i];
          sxx = lcase[lc].dat[0][nid]; syy = lcase[lc].dat[1][nid]; szz = lcase[lc].dat[2][nid];
          sxy = lcase[lc].dat[3][nid]; syz = lcase[lc].dat[4][nid]; szx = lcase[lc].dat[5][nid];
          calc_principal_and_mises(sxx, syy, szz, sxy, syz, szx, &p1, &p2, &p3, &vm);
          fprintf(fp, "          %.6e\n", p2);
        }
        fprintf(fp, "        </DataArray>\n");

        fprintf(fp, "        <DataArray type=\"Float32\" Name=\"PrincipalStress_3\" NumberOfComponents=\"1\" format=\"ascii\">\n");
        for (i = 0; i < num_pts; i++)
        {
          nid = pts_nodenr[i];
          sxx = lcase[lc].dat[0][nid]; syy = lcase[lc].dat[1][nid]; szz = lcase[lc].dat[2][nid];
          sxy = lcase[lc].dat[3][nid]; syz = lcase[lc].dat[4][nid]; szx = lcase[lc].dat[5][nid];
          calc_principal_and_mises(sxx, syy, szz, sxy, syz, szx, &p1, &p2, &p3, &vm);
          fprintf(fp, "          %.6e\n", p3);
        }
        fprintf(fp, "        </DataArray>\n");
      }
      /* Scalar Datasets (Temperature, PEEQ, Pressure, etc.) */
      else if (lcase[lc].ncomps == 1)
      {
        fprintf(fp, "        <DataArray type=\"Float32\" Name=\"%s\" NumberOfComponents=\"1\" format=\"ascii\">\n", lcase[lc].name);
        for (i = 0; i < num_pts; i++)
        {
          nid = pts_nodenr[i];
          fprintf(fp, "          %.6e\n", lcase[lc].dat[0][nid]);
        }
        fprintf(fp, "        </DataArray>\n");
      }
    }

    /* Include Frequency_Hz as accessible PointData field for modal analysis */
    if (analysis_type >= 2)
    {
      fprintf(fp, "        <DataArray type=\"Float32\" Name=\"Frequency_Hz\" NumberOfComponents=\"1\" format=\"ascii\">\n");
      for (i = 0; i < num_pts; i++)
      {
        fprintf(fp, "          %.6e\n", step_val);
      }
      fprintf(fp, "        </DataArray>\n");
    }
  }
  fprintf(fp, "      </PointData>\n");

  /* 4. Cell Data (Element IDs & Materials) */
  fprintf(fp, "      <CellData>\n");
  fprintf(fp, "        <DataArray type=\"Int32\" Name=\"Element_ID\" format=\"ascii\">\n");
  for (i = 0; i < num_elems; i++)
  {
    eid = elem_indices[i];
    fprintf(fp, "          %d\n", elem[eid].nr);
  }
  fprintf(fp, "        </DataArray>\n");

  fprintf(fp, "        <DataArray type=\"Int32\" Name=\"Material_ID\" format=\"ascii\">\n");
  for (i = 0; i < num_elems; i++)
  {
    eid = elem_indices[i];
    fprintf(fp, "          %d\n", elem[eid].mat);
  }
  fprintf(fp, "        </DataArray>\n");
  fprintf(fp, "      </CellData>\n");

  fprintf(fp, "    </Piece>\n");
  fprintf(fp, "  </UnstructuredGrid>\n");
  fprintf(fp, "</VTKFile>\n");

  fclose(fp);
  printf("  -> Written: %s (%d nodes, %d cells)\n", filename, num_pts, num_elems);
  return 1;
}

/* Structure to group datasets belonging to the same physical step/frame */
typedef struct {
  int    step_number;
  int    analysis_type;
  double value;
  char   name[MAX_LINE_LENGTH];
  int    num_lcs;
  int    lcs[64];
} PhysStep;

/* Main entry point for send <set> vtu/vtk [all] */
int write2vtu(char *setname, char *format, int strings, char **string, Summen *anz,
              Nodes *node, Faces *face, Elements *elem, Sets *set, Datasets *lcase)
{
  int setNr, i, j, k, eid, nid, n_nodes, vtk_type;
  int num_pts = 0, num_elems = 0;
  int *nodeMap = NULL;
  int *pts_nodenr = NULL;
  int *elem_indices = NULL;
  int export_all_steps = 0;
  char ext[8] = "vtu";
  char vtu_filename[MAX_LINE_LENGTH];
  char pvd_filename[MAX_LINE_LENGTH];
  FILE *pvd_fp = NULL;

  PhysStep *psteps = NULL;
  int num_psteps = 0;
  int active_pstep = 0;

  if (format != NULL && compare(format, "vtk", 3) == 3) strcpy(ext, "vtk");
  else if (format != NULL && compare(format, "vtu", 3) == 3) strcpy(ext, "vtu");

  if (anz->n == 0 || anz->e == 0)
  {
    printf("ERROR: No mesh available to export.\n");
    return 0;
  }

  setNr = getSetNr(setname);
  if (setNr < 0)
  {
    printf("ERROR: Set %s does not exist.\n", setname);
    return 0;
  }

  /* Check if \"all\" steps requested */
  if (strings > 0 && string != NULL && string[0] != NULL)
  {
    if (compare(string[0], "all", 3) == 3)
    {
      export_all_steps = 1;
    }
  }

  printf("\n=== Exporting to VTK XML Unstructured Grid (.%s) ===\n", ext);
  printf("Set: %s (mode: %s)\n", setname, export_all_steps ? "all steps (.pvd)" : "active step (.vtu)");

  /* 1. Identify Elements in Set */
  num_elems = set[setNr].anz_e;
  if (num_elems <= 0)
  {
    printf("ERROR: Set %s contains no elements.\n", setname);
    return 0;
  }
  elem_indices = set[setNr].elem;

  /* 2. Map Unique Nodes Used in Set */
  nodeMap = (int *)malloc((anz->nmax + 1) * sizeof(int));
  for (i = 0; i <= anz->nmax; i++) nodeMap[i] = -1;

  /* Mark all nodes referenced by elements */
  for (i = 0; i < num_elems; i++)
  {
    eid = elem_indices[i];
    vtk_type = get_vtk_cell_type(elem[eid].type, &n_nodes);
    if (vtk_type == 0) continue;
    for (j = 0; j < n_nodes; j++)
    {
      nid = elem[eid].nod[j];
      if (nid > 0 && nid <= anz->nmax && nodeMap[nid] == -1)
      {
        nodeMap[nid] = num_pts++;
      }
    }
  }

  if (num_pts == 0)
  {
    printf("ERROR: No valid points found in set %s.\n", setname);
    free(nodeMap);
    return 0;
  }

  /* Create array of user node IDs indexed by local 0-based point ID */
  pts_nodenr = (int *)malloc(num_pts * sizeof(int));
  for (i = 0; i <= anz->nmax; i++)
  {
    if (nodeMap[i] >= 0 && nodeMap[i] < num_pts)
    {
      pts_nodenr[nodeMap[i]] = i;
    }
  }

  /* 3. Group Datasets into Physical Steps / Frames */
  if (anz->l > 0 && lcase != NULL)
  {
    psteps = (PhysStep *)calloc(anz->l, sizeof(PhysStep));
    for (i = 0; i < anz->l; i++)
    {
      int matched = -1;

      for (j = 0; j < num_psteps; j++)
      {
        /* Datasets with same step_number belong to the same physical step/mode */
        if (psteps[j].step_number == lcase[i].step_number)
        {
          matched = j;
          break;
        }
      }

      if (matched >= 0)
      {
        if (psteps[matched].num_lcs < 64)
        {
          psteps[matched].lcs[psteps[matched].num_lcs++] = i;
        }
      }
      else
      {
        psteps[num_psteps].step_number = lcase[i].step_number;
        psteps[num_psteps].analysis_type = lcase[i].analysis_type;
        psteps[num_psteps].value = (double)lcase[i].value;
        strcpy(psteps[num_psteps].name, lcase[i].analysis_name);
        psteps[num_psteps].lcs[0] = i;
        psteps[num_psteps].num_lcs = 1;
        num_psteps++;
      }
    }

    /* Find which physical step contains the current active dataset */
    for (j = 0; j < num_psteps; j++)
    {
      for (k = 0; k < psteps[j].num_lcs; k++)
      {
        if (psteps[j].lcs[k] == cur_lc)
        {
          active_pstep = j;
          break;
        }
      }
    }
  }

  /* 4. Export Data */
  if (export_all_steps && num_psteps > 0)
  {
    /* Write PVD collection index file */
    sprintf(pvd_filename, "%s.pvd", setname);
    pvd_fp = fopen(pvd_filename, "w");
    if (pvd_fp)
    {
      fprintf(pvd_fp, "<?xml version=\"1.0\"?>\n");
      fprintf(pvd_fp, "<VTKFile type=\"Collection\" version=\"0.1\" byte_order=\"LittleEndian\">\n");
      fprintf(pvd_fp, "  <Collection>\n");
    }

    /* Write individual physical step files */
    for (i = 0; i < num_psteps; i++)
    {
      sprintf(vtu_filename, "%s_step_%03d.%s", setname, i + 1, ext);
      write_single_vtu_file(vtu_filename, psteps[i].num_lcs, psteps[i].lcs,
                            psteps[i].value, psteps[i].step_number, psteps[i].analysis_type,
                            anz, node, elem, set, setNr, lcase,
                            num_pts, pts_nodenr, nodeMap, num_elems, elem_indices);

      if (pvd_fp)
      {
        /* In ParaView PVD, each frame MUST have a unique timestep value.
           If time values are not strictly increasing (e.g. modal analysis with duplicate frequencies),
           use the 1-based step index to ensure ParaView loads each mode as an independent frame. */
        double tval = (double)(i + 1);
        if (num_psteps > 1 && psteps[num_psteps - 1].value > psteps[0].value)
        {
          /* Check if all time values are strictly increasing */
          int strictly_increasing = 1;
          for (j = 1; j < num_psteps; j++)
          {
            if (psteps[j].value <= psteps[j - 1].value) { strictly_increasing = 0; break; }
          }
          if (strictly_increasing) tval = psteps[i].value;
        }

        fprintf(pvd_fp, "    <DataSet timestep=\"%.6f\" group=\"\" part=\"0\" file=\"%s\"/>\n",
                tval, vtu_filename);
      }
    }

    if (pvd_fp)
    {
      fprintf(pvd_fp, "  </Collection>\n");
      fprintf(pvd_fp, "</VTKFile>\n");
      fclose(pvd_fp);
      printf("  -> Written PVD collection: %s (%d physical steps)\n", pvd_filename, num_psteps);
    }
  }
  else
  {
    /* Single active step / mesh */
    sprintf(vtu_filename, "%s.%s", setname, ext);
    if (num_psteps > 0)
    {
      write_single_vtu_file(vtu_filename, psteps[active_pstep].num_lcs, psteps[active_pstep].lcs,
                            psteps[active_pstep].value, psteps[active_pstep].step_number, psteps[active_pstep].analysis_type,
                            anz, node, elem, set, setNr, lcase,
                            num_pts, pts_nodenr, nodeMap, num_elems, elem_indices);
    }
    else
    {
      write_single_vtu_file(vtu_filename, 0, NULL,
                            0.0, 0, 0,
                            anz, node, elem, set, setNr, lcase,
                            num_pts, pts_nodenr, nodeMap, num_elems, elem_indices);
    }
  }

  printf("=== VTK/VTU Export Complete ===\n\n");

  if (psteps) free(psteps);
  free(nodeMap);
  free(pts_nodenr);
  return 1;
}
