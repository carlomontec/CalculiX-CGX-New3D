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

/* Write a single .vtu file for a specific step or current dataset */
static int write_single_vtu_file(const char *filename, int target_step,
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
  if (anz->l > 0 && lcase != NULL)
  {
    for (lc = 0; lc < anz->l; lc++)
    {
      /* Filter datasets by target_step if specified (target_step > 0) */
      if (target_step > 0 && lcase[lc].step_number != target_step) continue;
      if (target_step == 0 && lc != cur_lc && anz->l > 1)
      {
        if (lcase[lc].step_number != lcase[cur_lc].step_number) continue;
      }

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

/* Main entry point for send <set> vtu [all] */
int write2vtu(char *setname, int strings, char **string, Summen *anz,
              Nodes *node, Faces *face, Elements *elem, Sets *set, Datasets *lcase)
{
  int setNr, i, j, k, eid, nid, n_nodes, vtk_type;
  int num_pts = 0, num_elems = 0;
  int *nodeMap = NULL;
  int *pts_nodenr = NULL;
  int *elem_indices = NULL;
  int export_all_steps = 0;
  int num_steps = 0, *step_list = NULL;
  double *step_times = NULL;
  char vtu_filename[MAX_LINE_LENGTH];
  char pvd_filename[MAX_LINE_LENGTH];
  FILE *pvd_fp = NULL;

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

  printf("\n=== Exporting to VTK XML Unstructured Grid (.vtu) ===\n");
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

  /* 3. Export Data */
  if (export_all_steps && anz->l > 0)
  {
    /* Find unique steps */
    step_list = (int *)malloc(anz->l * sizeof(int));
    step_times = (double *)malloc(anz->l * sizeof(double));
    for (i = 0; i < anz->l; i++)
    {
      int step = lcase[i].step_number;
      int exists = 0;
      for (j = 0; j < num_steps; j++)
      {
        if (step_list[j] == step) { exists = 1; break; }
      }
      if (!exists)
      {
        step_list[num_steps] = step;
        step_times[num_steps] = (double)lcase[i].value;
        num_steps++;
      }
    }

    /* Write PVD collection index file */
    sprintf(pvd_filename, "%s.pvd", setname);
    pvd_fp = fopen(pvd_filename, "w");
    if (pvd_fp)
    {
      fprintf(pvd_fp, "<?xml version=\"1.0\"?>\n");
      fprintf(pvd_fp, "<VTKFile type=\"Collection\" version=\"0.1\" byte_order=\"LittleEndian\">\n");
      fprintf(pvd_fp, "  <Collection>\n");
    }

    /* Write individual step files */
    for (i = 0; i < num_steps; i++)
    {
      sprintf(vtu_filename, "%s_step_%03d.vtu", setname, step_list[i]);
      write_single_vtu_file(vtu_filename, step_list[i], anz, node, elem, set, setNr,
                            lcase, num_pts, pts_nodenr, nodeMap, num_elems, elem_indices);

      if (pvd_fp)
      {
        fprintf(pvd_fp, "    <DataSet timestep=\"%.6f\" group=\"\" part=\"0\" file=\"%s\"/>\n",
                step_times[i], vtu_filename);
      }
    }

    if (pvd_fp)
    {
      fprintf(pvd_fp, "  </Collection>\n");
      fprintf(pvd_fp, "</VTKFile>\n");
      fclose(pvd_fp);
      printf("  -> Written PVD collection: %s (%d steps)\n", pvd_filename, num_steps);
    }

    free(step_list);
    free(step_times);
  }
  else
  {
    /* Single active step / mesh */
    sprintf(vtu_filename, "%s.vtu", setname);
    write_single_vtu_file(vtu_filename, 0, anz, node, elem, set, setNr,
                          lcase, num_pts, pts_nodenr, nodeMap, num_elems, elem_indices);
  }

  printf("=== VTK/VTU Export Complete ===\n\n");

  free(nodeMap);
  free(pts_nodenr);
  return 1;
}
