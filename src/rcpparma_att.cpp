// -*- mode: C++; c-indent-level: 4; c-basic-offset: 4; indent-tabs-mode: nil; -*-

// we only include RcppArmadillo.h which pulls Rcpp.h in for us
#include "RcppArmadillo.h"
#include "stdlib.h"

using namespace Rcpp;
using namespace arma;

// via the depends attribute we tell Rcpp to create hooks for
// RcppArmadillo so that the build process will know what to do
//
// [[Rcpp::depends(RcppArmadillo)]]

// Function to update support_ss if necessary
void updateSupport(std::vector<int>& support_ss, const std::vector<int>& sel_b) {
  // Create an unordered set from support_ss for O(1) lookups
  std::unordered_set<int> support_set(support_ss.begin(), support_ss.end());
  
  // Flag to check if all elements are found
  bool all_found = true;
  
  // Check if all elements in sel_b are in support_ss
  for (int num : sel_b) {
    if (support_set.find(num) == support_set.end()) { // Element not found
      all_found = false;
      break; // Exit early if at least one element is missing
    }
  }
  
  // If all elements are already present, return early
  if (all_found) {
    return;
  }
  
  // Otherwise, add missing elements to support_ss
  for (int num : sel_b) {
    if (support_set.find(num) == support_set.end()) {
      support_ss.push_back(num);
      support_set.insert(num);
    }
  }
}

bool haveSameElements(std::vector<int> sel_b, std::vector<int> support_ss) {
  return std::multiset<int>(sel_b.begin(), sel_b.end()) == std::multiset<int>(support_ss.begin(), support_ss.end());
}

// Function to compute LS estimator of sigma^2
double get_LSsigma2(const arma::colvec& y, const arma::mat& X) {
  int n = X.n_rows; 
  int p = X.n_cols; 
  
  arma::mat XtX_inv = arma::solve(X.t() * X, arma::eye(X.n_cols, X.n_cols));
  arma::mat H = X * XtX_inv * X.t();
  
  arma::mat I = eye(n, n);
  arma::mat M = I - H;
  
  double sigma2_hat = as_scalar(y.t() * M * y) / (n - p);
  
  return sigma2_hat;
}

std::vector<arma::uvec> get_sorted_indices(const arma::mat& R) {
  std::vector<arma::uvec> sorted_indices_per_col;
  
  for (arma::uword j = 0; j < R.n_cols; ++j) {
    arma::vec col = arma::abs(R.col(j));
    col.shed_row(j);  // Exclude diagonal
    arma::uvec indices = arma::sort_index(col, "descend");
    sorted_indices_per_col.push_back(indices);
  }
  
  return sorted_indices_per_col;
}

double avg_offd_abs(const arma::mat& W) {
  if (W.n_rows != W.n_cols) {
    throw std::invalid_argument("Matrix must be square.");
  }
  arma::mat abs_W = arma::abs(W);
  arma::mat diag_abs_W = arma::diagmat(abs_W);
  
  return arma::accu(abs_W - diag_abs_W) / (W.n_elem - W.n_rows);
}

//' Inner Loop using Autotune Lasso  
// [[Rcpp::export]]
List lasso_autotune(const arma::mat& X_X, const arma::colvec& X_Y, const arma::uvec& r_XY, 
                     double sigma2, int n, double s_22, 
                     const arma::colvec& y, const arma::mat& Z, 
                     int node, int outer_iter, 
                     double alpha, const arma::vec& F_crit_values, 
                     double lambda0 = -1, 
                     bool verbose_i = false) {
   
   int p = X_X.n_rows;
   int d = std::min(n-2, p);
   arma::colvec X_r_old = X_Y;
   arma::vec b_old = arma::zeros<arma::colvec>(p);
   arma::vec b = b_old;
   double e_old = 1e6;
   double sel_sigma2 = var(y);
   double sigma2_old = 1e6;
   bool F_test = true;
   double thresh = -1;
   std::vector<int> support_ss;
   std::vector<int> support_ss_old;
   std::vector<int> sel_b;
   
   if (lambda0 == -1) {
     lambda0 = max(abs(X_Y)) * (1 / s_22);
   }
   
   for (int iter = 0; iter <= 1000; iter++) {
     
     // if (verbose_i) {
     //   Rcout << "node " << node+1 << " inner iter " << iter+1 << " sigma2 " << sigma2 << " e_old " << e_old << std::endl;
     // }
     
     // if (e_old > 0.0001) {
     b = b_old;
     vec X_r = X_r_old;
     vec sd_r = zeros<vec>(p);
     
     thresh = lambda0 * sigma2;
     for (int j = 0; j < p; j++) {
       X_r += X_X.col(j) * b[j];
       double X_rj = X_r[j];
       
       if (abs(X_rj) < 0.5 * thresh) {
         b[j] = 0;
       } else {
         b[j] = (abs(X_rj) - 0.5 * thresh) * ((X_rj > 0) ? 1 : -1) / X_X(j, j);
       }
       
       X_r -= X_X.col(j) * b[j];
       if (F_test) {
         arma::colvec pr = y - Z.cols(find(linspace<uvec>(0, p - 1, p) != j)) * b(find(linspace<uvec>(0, p - 1, p) != j));
         sd_r[j] = sqrt(as_scalar(pr.t() * pr) / n);
       }
     }
     
     
     if (F_test) {
       arma::uvec sorted_sd_idx;
       // sorted_sd_idx = sort_index(sd_r, "descend");
       if (iter == 0) {
         sorted_sd_idx = r_XY;
       } else {
         // sorted_sd_idx = sort_index(sd_r, "descend");
       }
       
       // if (verbose_i) {
       //   Rcout << "sorted_sd_idx with sd_val: ";
       //   for (int i = 0; i < 5; i++) {
       //     Rcpp::Rcout << sorted_sd_idx[i] + 1 << " " 
       //                 << sd_r[sorted_sd_idx[i]] << " "<< b[sorted_sd_idx[i]] << ", ";
       //   }
       //   Rcpp::Rcout << std::endl; 
       // }
       
       std::vector<int> sel_b;
       std::vector<int> new_b = sel_b;
       sel_sigma2 = var(y);
       
       arma::uvec sel_idx;
       // Sequential F test for variable selection
       for (size_t j = 0; j < d; j++) {
         // Set sigma2 to sigma2 ols 
         sigma2 = sel_sigma2;
         
         // Find index of the maximum element
         auto max_it = std::max_element(sd_r.begin(), sd_r.end());
         int max_idx = std::distance(sd_r.begin(), max_it);
         // Set the maximum element to -1
         sd_r[max_idx] = -1;
         
         arma::uword j_idx;
         if (iter == 0) {
           // int j_idx = sorted_sd_idx[j];
           j_idx = sorted_sd_idx[j];
         } else {
           // int j_idx = max_idx;
           j_idx = max_idx;
         }
         new_b.push_back(static_cast<int>(j_idx));
         sel_idx = arma::join_vert(sel_idx, arma::uvec{j_idx});
         // double new_sigma2 = get_LSsigma2(y, Z.cols(sorted_sd_idx.subvec(0, j)));
         double new_sigma2 = get_LSsigma2(y, Z.cols(sel_idx));
         double F_stat = (sel_sigma2 - new_sigma2) / (new_sigma2 / (n-(j+1)));
         
         if (F_stat > F_crit_values[j]) {
           sel_b = new_b;
           sel_sigma2 = new_sigma2;
         } else {
           // if (verbose_i) {
           //   Rcout << "selected b: ";
           //   for (int i = 0; i < sel_b.size(); i++) {
           //     Rcpp::Rcout << sel_b[i] + 1 << " " << b[sel_b[i]] << " ";
           //   }
           //   Rcpp::Rcout << std::endl; 
           // }
           break;
         }
       }
       
       // if (verbose_i) {
       //   Rcout << "previous support super set: ";
       //   for (int i = 0; i < support_ss.size(); i++) {
       //     Rcpp::Rcout << support_ss[i] + 1 << " ";
       //   }
       //   Rcpp::Rcout << std::endl; 
       // }
       
       updateSupport(support_ss, sel_b);
       if (iter > 0) {
         // Check if support supper set converges
         if (haveSameElements(support_ss, support_ss_old)) {
           F_test = false;
           // if (verbose_i) {
           //   Rcout << "support super set converges: ";
           //   for (int i = 0; i < support_ss.size(); i++) {
           //     Rcpp::Rcout << support_ss[i] + 1 << " ";
           //   }
           //   Rcpp::Rcout << std::endl; 
           // }
         } 
         
         // Check if support supper set converges
         if (abs(sel_sigma2 - sigma2_old) < 0.0001) {
           F_test = false;
           // if (verbose_i) {
           //   Rcout << "sigma2 value converges" << std::endl; 
           // }
         }
       }
     }
     
     b_old = b;
     X_r_old = X_r;
     sigma2_old = sel_sigma2;
     support_ss_old = support_ss;
     double e = mean(square(X_r_old));
     
     if (abs(e - e_old) > 0.0001) {
       e_old = e;
     } else {
       break;
     }
   }
   
   return List::create(Named("b")=b,
                       Named("sigma2")=sigma2,
                       Named("lambda")=thresh);
 }


//' Autotune Graphical Lasso 
//'
//' @param X Data matrix 
//' @param alpha alpha value of F test. Default value is 0.02.
//' @param thr Threshold for convergence. Default value is 1e-4. Iterations stop when average absolute parameter change is less than thr * ave(abs(offdiag(s)))
//' @param maxit Maximum number of iterations of outer loop. Default 100.
//' @return Estimated precision matrix
// [[Rcpp::export]]
List glasso_autotune(const arma::mat& X, double alpha = 0.02, 
                      double penalize_diag = true,
                      double thr = 0.05, int maxit = 50, 
                      bool verbose = true, bool verbose_i = false) {
   
   int n = X.n_rows;
   int p = X.n_cols;
   
   Rcpp::Function qf("qf");
   // Prevent non-positive df2 of F-test
   int d = std::min(n-2, p-1);
   arma::vec F_crit_values = arma::linspace<arma::vec>(1, d, d);  
   F_crit_values.transform([&](double j) { 
     return Rcpp::as<double>(qf(1 - alpha, 1, n-(j+1)));
   });
   
   arma::mat S = (X.t() * X) / n;  
   arma::mat R = cor(X);
   arma::vec sigma2_hat = S.diag();
   
   arma::mat W = S;
   arma::mat Theta = zeros<mat>(p, p);
   
   double e_old = 1e6;
   double e = 0;
   bool final_cycle = false;
   bool valid_diag = true;
   int niter = -1;
   
   double lambda_tmp = -1;
   arma::vec lambdav(p);
   lambdav.fill(-1);
   arma::vec b_hat(p-1, fill::zeros);
   
   arma::mat W_old = W;
   
   if (penalize_diag) {
     for (int j = 0; j < p; j++) {
       arma::uvec idx = regspace<uvec>(0, p - 1);
       idx.shed_row(j);
       arma::colvec s_12 = S.submat(idx, uvec{(unsigned int)j});
       W(j, j) = W(j, j) + 0.5 * max(abs(s_12)) ;
     }
   }
   
   // Sort indices of cor matrix
   std::vector<arma::uvec> R_sorted_idx = get_sorted_indices(R);
   
   for (int iter = 0; iter < maxit; iter++) {
     if (verbose) {
       Rcout << "glasso iter = " << iter+1 << "; error = " << round(e_old * 1000) / 1000 << std::endl;
     }
     
     for (int j = 0; j < p; j++) {
       arma::uvec idx = regspace<uvec>(0, p - 1);
       idx.shed_row(j);
       
       arma::mat W_11 = W(idx, idx);
       arma::colvec s_12 = S.submat(idx, uvec{(unsigned int)j});
       // arma::colvec r_12 = R.submat(idx, uvec{(unsigned int)j});
       double s_22 = S(j, j);
       
       // std::cout << "Test: " << R_sorted_idx[j].size() << std::endl;
       
       List fitted = lasso_autotune(W_11, s_12, R_sorted_idx[j],
                                    sigma2_hat(j), n, s_22, 
                                    X.col(j), X.cols(idx), 
                                    j, iter, alpha, F_crit_values,
                                    lambdav(j), verbose_i);
       
       b_hat = as<arma::vec>(fitted["b"]);
       sigma2_hat(j) = fitted["sigma2"];
       lambda_tmp = fitted["lambda"];
       lambdav(j) = lambda_tmp / sigma2_hat(j);
       
       arma::mat Wsub = W_11 * b_hat;
       W.submat(idx, uvec{(unsigned int)j}) = Wsub;
       W.submat(uvec{(unsigned int)j}, idx) = trans(Wsub);
       
       if (final_cycle) {
         Theta(j, j) = 1.0 / (W(j, j) - dot(W.submat(idx, uvec{(unsigned int)j}), b_hat));
         arma::mat Thetasub = -Theta(j, j) * b_hat;
         Theta.submat(idx, uvec{(unsigned int)j}) = Thetasub;
         Theta.submat(uvec{(unsigned int)j}, idx) = trans(Thetasub);
         
         if (Theta(j, j) < 0) {
           Rcout << "Diagonal of node " << j + 1 << " is negative. Consider a smaller alpha threshold!" << std::endl;
           valid_diag = false;
         }
         
       }
       
     }
     
     // e = norm(W - W_old, "fro");
     // if (verbose) {
     //   Rcout << "change in W.err = " << std::abs(e - e_old) << std::endl;
     // }
     
     arma::mat W_diff = W - W_old;
     // e = arma::abs(W_diff).max() / arma::abs(W_old).max();
     // e = avg_offd_abs(W_diff) / avg_offd_abs(W_old);
     e = norm(W_diff, "fro") / norm(W_old, "fro");
     // if (verbose) {
     //   Rcout << "change in W.err = " << e << std::endl;
     // }
     
     if (final_cycle) { 
       if (iter < maxit-1) {
         niter = iter + 1;
       }
       break;
     }
     
     // if (std::abs(e - e_old) < thr) {
     if (e < thr) {
       final_cycle = true;
     } else {
       W_old = W;
       e_old = e;
     }
     if (iter == maxit-2) {
       final_cycle = true;
     }
   }
   
   bool converged = (niter != -1);
   if (converged == 0) {
     Rcout << "did not converged; final glasso iter = " << maxit << std::endl;
   } else {
     Rcout << "final glasso iter = " << niter << std::endl;
   }
   if (!valid_diag) {
     converged = 0;
   }
   return List::create(Named("Theta") = Theta, 
                       Named("sigma2.hat") = sigma2_hat,
                       Named("niter") = niter,
                       Named("converged") = converged);
 }
